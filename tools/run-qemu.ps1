param(
    [string]$ImageFile
)

if ([string]::IsNullOrWhiteSpace($ImageFile) -or $args.Count -ne 0) {
    [Console]::Error.WriteLine("Usage: {0} <image_file>", $MyInvocation.MyCommand.Name)
    exit 1
}

$QemuDirectory = 'C:\Program Files\qemu'
$QemuExecutable = Join-Path $QemuDirectory 'qemu-system-x86_64.exe'
$OvmfCode = Join-Path $QemuDirectory 'share\edk2-x86_64-code.fd'
# QEMU's x86-64 firmware uses the shared i386 variables template.
$OvmfVarsOriginal = Join-Path $QemuDirectory 'share\edk2-i386-vars.fd'
$OvmfVars = Join-Path (Get-Location).Path 'edk2-i386-vars.fd'

try {
    foreach ($Path in @($QemuExecutable, $OvmfCode, $OvmfVarsOriginal, $ImageFile)) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "File not found: $Path"
        }
    }

    if (-not (Test-Path -LiteralPath $OvmfVars)) {
        try {
            Copy-Item -LiteralPath $OvmfVarsOriginal -Destination $OvmfVars -ErrorAction Stop
        }
        catch {
            throw "Unable to create OVMF variables file: $OvmfVars. $($_.Exception.Message)"
        }
    }

    try {
        $VarsStream = [System.IO.File]::Open($OvmfVars, 'Open', 'Write', 'None')
        $VarsStream.Dispose()
    }
    catch {
        throw "OVMF variables file is not writable: $OvmfVars. $($_.Exception.Message)"
    }

    # QEMU requires commas within -drive filenames to be doubled.
    $CodePath = $OvmfCode.Replace(',', ',,')
    $VarsPath = $OvmfVars.Replace(',', ',,')
    $ImagePath = (Get-Item -LiteralPath $ImageFile -ErrorAction Stop).FullName.Replace(',', ',,')
    $QemuArguments = @(
        '-D', './log.txt', '-monitor', 'stdio', '-smp', '1', '-m', '4096'
        '-net', 'none', '-no-reboot'
        '-drive', "if=pflash,format=raw,readonly=on,file=$CodePath"
        '-drive', "if=pflash,format=raw,file=$VarsPath"
        '-drive', "if=virtio,file=$ImagePath,format=raw"
        '-serial', 'file:./qserial.txt'
    )

    & $QemuExecutable @QemuArguments
    exit $LASTEXITCODE
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
