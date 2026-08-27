#!/usr/bin/env bash
set -Eeuo pipefail

readonly SCRIPT_VERSION="1.0.0"
readonly DEFAULT_EFI_SOURCE="/usr/share/limine/BOOTX64.EFI"

device=""
efi_source="$DEFAULT_EFI_SOURCE"
apply_changes=false
mountpoint=""
mounted_by_script=false

usage() {
    cat <<'EOF'
Repair the Limine files on an existing FAT EFI partition without formatting it.

Usage:
  repair-limine-usb.sh --device /dev/sdXN [--efi FILE] [--apply]
  repair-limine-usb.sh --help
  repair-limine-usb.sh --version

Options:
  --device DEVICE  Target partition, for example /dev/sda1.
  --efi FILE       Clean BOOTX64.EFI source (default: /usr/share/limine/BOOTX64.EFI).
  --apply          Back up and rewrite boot files. Without it, only inspect.
  --help           Show this help.
  --version        Show the script version.
EOF
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

cleanup() {
    if [[ "$mounted_by_script" == true && -n "$mountpoint" ]]; then
        udisksctl unmount -b "$device" >/dev/null || true
    fi
}

parse_arguments() {
    while (($# > 0)); do
        case "$1" in
            --device)
                (($# >= 2)) || fail "--device requires a value"
                device="$2"
                shift 2
                ;;
            --efi)
                (($# >= 2)) || fail "--efi requires a value"
                efi_source="$2"
                shift 2
                ;;
            --apply)
                apply_changes=true
                shift
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            --version)
                printf '%s\n' "$SCRIPT_VERSION"
                exit 0
                ;;
            *)
                fail "unknown argument: $1"
                ;;
        esac
    done
}

validate_target() {
    [[ -n "$device" ]] || fail "--device is required"
    [[ -b "$device" ]] || fail "$device is not a block device"
    [[ "$(lsblk -dnro TYPE "$device")" == "part" ]] || fail "$device is not a partition"

    local filesystem
    filesystem="$(lsblk -dnro FSTYPE "$device")"
    [[ "$filesystem" == "vfat" ]] || fail "$device has '$filesystem', expected vfat"

    [[ -r "$efi_source" ]] || fail "cannot read EFI source: $efi_source"
    [[ "$(od -An -tx1 -N2 "$efi_source" | tr -d ' \n')" == "4d5a" ]] || fail "EFI source has no MZ signature"

    printf 'Target: %s\n' "$(lsblk -dnro PATH,SIZE,FSTYPE,LABEL,MODEL "$device")"
}

discover_mountpoint() {
    mountpoint="$(findmnt -nro TARGET --source "$device" || true)"
    if [[ -n "$mountpoint" ]]; then
        return
    fi

    local output
    output="$(udisksctl mount -b "$device" -o ro)"
    mountpoint="${output#* at }"
    mountpoint="${mountpoint%.}"
    [[ -d "$mountpoint" ]] || fail "could not determine mount point from: $output"
    mounted_by_script=true
}

inspect_files() {
    printf 'Mounted at: %s\n' "$mountpoint"
    find "$mountpoint" -maxdepth 4 -type f -printf '%P\t%s bytes\n' | sort

    if [[ -f "$mountpoint/EFI/BOOT/BOOTX64.EFI" ]]; then
        printf 'Installed EFI SHA-256: '
        sha256sum "$mountpoint/EFI/BOOT/BOOTX64.EFI" | cut -d' ' -f1
    fi
    printf 'Source EFI SHA-256:    '
    sha256sum "$efi_source" | cut -d' ' -f1
}

remount_read_write() {
    udisksctl unmount -b "$device" >/dev/null
    local output
    output="$(udisksctl mount -b "$device")"
    mountpoint="${output#* at }"
    mountpoint="${mountpoint%.}"
    [[ -d "$mountpoint" ]] || fail "could not determine writable mount point from: $output"
    mounted_by_script=true
    [[ -w "$mountpoint" ]] || fail "mount point is not writable: $mountpoint"
}

write_config() {
    local destination="$1"
    install -d "$(dirname "$destination")"
    printf '%s\n' \
        'timeout: 10' \
        'verbose: yes' \
        '/PureC OS' \
        '    protocol: limine' \
        '    kernel_path: boot():/boot/kernel.elf' >"$destination"
}

back_up_boot_files() {
    local timestamp backup_dir
    timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
    backup_dir="$(pwd)/repair-backups/${timestamp}"
    install -d "$backup_dir"

    if [[ -d "$mountpoint/EFI" ]]; then
        cp -a "$mountpoint/EFI" "$backup_dir/"
    fi
    if [[ -d "$mountpoint/BOOT" ]]; then
        cp -a "$mountpoint/BOOT" "$backup_dir/"
    fi
    if [[ -f "$mountpoint/limine.conf" ]]; then
        cp -a "$mountpoint/limine.conf" "$backup_dir/"
    fi

    printf 'Backup: %s\n' "$backup_dir"
}

apply_repair() {
    remount_read_write
    back_up_boot_files

    install -d "$mountpoint/EFI/BOOT"
    install -m 0644 "$efi_source" "$mountpoint/EFI/BOOT/BOOTX64.EFI"

    local relative_path
    for relative_path in \
        limine.conf \
        boot/limine/limine.conf \
        EFI/limine/limine.conf \
        EFI/BOOT/limine.conf \
        limine/limine.conf \
        boot/limine.conf; do
        write_config "$mountpoint/$relative_path"
    done

    sync "$mountpoint"

    local source_hash installed_hash
    source_hash="$(sha256sum "$efi_source" | cut -d' ' -f1)"
    installed_hash="$(sha256sum "$mountpoint/EFI/BOOT/BOOTX64.EFI" | cut -d' ' -f1)"
    [[ "$source_hash" == "$installed_hash" ]] || fail "EFI read-back hash mismatch"

    for relative_path in \
        limine.conf \
        boot/limine/limine.conf \
        EFI/limine/limine.conf \
        EFI/BOOT/limine.conf \
        limine/limine.conf \
        boot/limine.conf; do
        cmp -s "$mountpoint/limine.conf" "$mountpoint/$relative_path" || fail "config read-back mismatch: $relative_path"
    done

    printf 'Repair completed and read-back verified.\n'
}

main() {
    trap cleanup EXIT
    parse_arguments "$@"
    validate_target
    discover_mountpoint
    inspect_files

    if [[ "$apply_changes" != true ]]; then
        printf 'Inspection only. Add --apply to write the repair.\n'
        return
    fi

    apply_repair
}

main "$@"
