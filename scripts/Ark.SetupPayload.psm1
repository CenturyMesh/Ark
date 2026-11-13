Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# The layout of ARK's Windows setup (installer/ArkSetup/Core/Payload.cs), read and written by
# scripts/build-installer.ps1 and the installer tests:
#   stub (published single-file exe, possibly signed on its own) | package ZIP |
#   40-byte footer "ARKPAYL2" + payload offset (int64) + payload length (int64) + stub CheckSum (uint32)
#   + stub certificate table offset (uint32) + size (uint32) + 0 (uint32), little endian |
#   [signed setups only: up to 7 zero bytes, then the setup's Authenticode certificate table]
$script:ArkSetupFooterSize = 40
$script:ArkSetupMagic = 'ARKPAYL2'

function Read-ArkBytes([IO.Stream]$Stream, [long]$Offset, [int]$Count) {
    $buffer = New-Object byte[] $Count
    [void]$Stream.Seek($Offset, [IO.SeekOrigin]::Begin)
    $read = 0
    while ($read -lt $Count) {
        $n = $Stream.Read($buffer, $read, $Count - $read)
        if ($n -le 0) { throw 'Unexpected end of file.' }
        $read += $n
    }
    return ,$buffer
}

function Get-ArkPeLayout([string]$Path) {
    # The PE CheckSum and security (certificate table) directory, the fields Authenticode signing changes.
    $stream = [IO.File]::OpenRead($Path)
    try {
        $count = [int][Math]::Min(4096, $stream.Length)
        if ($count -lt 64) { return $null }
        $head = Read-ArkBytes $stream 0 $count
        if ($head[0] -ne 0x4D -or $head[1] -ne 0x5A) { return $null }
        $pe = [BitConverter]::ToInt32($head, 0x3C)
        if ($pe -lt 64 -or $pe -gt $count - 26 -or [Text.Encoding]::ASCII.GetString($head, $pe, 4) -cne "PE`0`0") { return $null }
        $optional = $pe + 24
        $optionalSize = [BitConverter]::ToUInt16($head, $pe + 20)
        $magic = [BitConverter]::ToUInt16($head, $optional)
        if ($magic -eq 0x10B) { $directories = $optional + 96 } elseif ($magic -eq 0x20B) { $directories = $optional + 112 } else { return $null }
        if ($directories + 40 -gt $optional + $optionalSize -or $directories + 40 -gt $count -or [BitConverter]::ToUInt32($head, $directories - 4) -lt 5) { return $null }
        $security = $directories + 32
        return [pscustomobject]@{
            ChecksumPos = [long]($optional + 64); SecurityDirPos = [long]$security
            Checksum = [BitConverter]::ToUInt32($head, $optional + 64)
            CertOffset = [BitConverter]::ToUInt32($head, $security); CertSize = [BitConverter]::ToUInt32($head, $security + 4)
            Length = [long]$stream.Length
        }
    } finally { $stream.Dispose() }
}

function Set-ArkPeField([string]$Path, [long]$Position, [uint32]$Value) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try { [void]$stream.Seek($Position, [IO.SeekOrigin]::Begin); $bytes = [BitConverter]::GetBytes($Value); $stream.Write($bytes, 0, 4) } finally { $stream.Dispose() }
}

function New-ArkSetupFooter([long]$Offset, [long]$Length, $StubLayout) {
    $footer = New-Object byte[] $script:ArkSetupFooterSize
    [Text.Encoding]::ASCII.GetBytes($script:ArkSetupMagic).CopyTo($footer, 0)
    [BitConverter]::GetBytes([int64]$Offset).CopyTo($footer, 8)
    [BitConverter]::GetBytes([int64]$Length).CopyTo($footer, 16)
    [BitConverter]::GetBytes([uint32]$StubLayout.Checksum).CopyTo($footer, 24)
    [BitConverter]::GetBytes([uint32]$StubLayout.CertOffset).CopyTo($footer, 28)
    [BitConverter]::GetBytes([uint32]$StubLayout.CertSize).CopyTo($footer, 32)
    return ,$footer
}

function Find-ArkSetupPayload([string]$Path) {
    # Finds the footer the way Setup does: at the end of the file, or right before the setup's certificate table
    # (after up to 7 zero bytes of alignment). Returns $null for files without a consistent ARK payload.
    $layout = Get-ArkPeLayout $Path
    if (-not $layout) { return $null }
    $end = $layout.Length
    if ($layout.CertSize -ne 0) {
        if ($layout.CertOffset % 8 -ne 0 -or [long]$layout.CertOffset + $layout.CertSize -ne $layout.Length) { return $null }
        $end = [long]$layout.CertOffset
    }
    $window = $script:ArkSetupFooterSize + 7
    if ($end -lt $window + $layout.SecurityDirPos + 8) { return $null }
    $stream = [IO.File]::OpenRead($Path)
    try { $tail = Read-ArkBytes $stream ($end - $window) $window } finally { $stream.Dispose() }
    for ($pad = 0; $pad -le 7; $pad++) {
        if ($pad -gt 0 -and ($tail[$window - $pad] -ne 0 -or $layout.CertSize -eq 0)) { break }
        $at = $window - $pad - $script:ArkSetupFooterSize
        if ([Text.Encoding]::ASCII.GetString($tail, $at, 8) -cne $script:ArkSetupMagic) { continue }
        $offset = [BitConverter]::ToInt64($tail, $at + 8); $length = [BitConverter]::ToInt64($tail, $at + 16)
        $stubCertOffset = [BitConverter]::ToUInt32($tail, $at + 28); $stubCertSize = [BitConverter]::ToUInt32($tail, $at + 32)
        $footer = $end - $pad - $script:ArkSetupFooterSize
        $stubOk = if ($stubCertSize -eq 0) { $stubCertOffset -eq 0 } else { $stubCertOffset % 8 -eq 0 -and [long]$stubCertOffset + $stubCertSize -eq $offset }
        if ($offset -lt $layout.SecurityDirPos + 8 -or $length -le 0 -or $offset + $length -ne $footer -or [BitConverter]::ToUInt32($tail, $at + 36) -ne 0 -or -not $stubOk) { return $null }
        return [pscustomobject]@{
            Offset = $offset; Length = $length; FooterOffset = $footer; Signed = ($layout.CertSize -ne 0)
            StubChecksum = [BitConverter]::ToUInt32($tail, $at + 24); StubCertOffset = $stubCertOffset; StubCertSize = $stubCertSize
            ChecksumPos = $layout.ChecksumPos; SecurityDirPos = $layout.SecurityDirPos
        }
    }
    return $null
}

function Test-ArkSetupFile([string]$Path) {
    # An ARK WPF setup: the current footer, or the 24-byte "ARKPAYLD" footer at the end of older ones.
    if (Find-ArkSetupPayload $Path) { return $true }
    $stream = [IO.File]::OpenRead($Path)
    try {
        if ($stream.Length -lt 24) { return $false }
        return ([Text.Encoding]::ASCII.GetString((Read-ArkBytes $stream ($stream.Length - 24) 24), 0, 8) -ceq 'ARKPAYLD')
    } finally { $stream.Dispose() }
}

function Get-ArkSetupRangeHash([string]$Path, [long]$Offset, [long]$Length, $Patch) {
    # SHA-256 of [Offset, Offset+Length), optionally with the stub's header fields restored ($Patch = Find result).
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try {
        [void]$stream.Seek($Offset, [IO.SeekOrigin]::Begin)
        $buffer = New-Object byte[] 1048576
        $left = $Length; $first = $true
        while ($left -gt 0) {
            $n = $stream.Read($buffer, 0, [int][Math]::Min($buffer.Length, $left))
            if ($n -le 0) { throw 'Unexpected end of file.' }
            if ($first -and $Patch) {
                [BitConverter]::GetBytes([uint32]$Patch.StubChecksum).CopyTo($buffer, [int]$Patch.ChecksumPos)
                [BitConverter]::GetBytes([uint32]$Patch.StubCertOffset).CopyTo($buffer, [int]$Patch.SecurityDirPos)
                [BitConverter]::GetBytes([uint32]$Patch.StubCertSize).CopyTo($buffer, [int]$Patch.SecurityDirPos + 4)
            }
            $first = $false
            [void]$sha.TransformBlock($buffer, 0, $n, $null, 0)
            $left -= $n
        }
        [void]$sha.TransformFinalBlock((New-Object byte[] 0), 0, 0)
        return -join ($sha.Hash | ForEach-Object { $_.ToString('x2') })
    } finally { $stream.Dispose(); $sha.Dispose() }
}

function Get-ArkSetupPayloadHash([string]$Path, $Payload) { Get-ArkSetupRangeHash $Path $Payload.Offset $Payload.Length $null }

function Get-ArkSetupStubHash([string]$Path, $Payload) {
    # The SHA-256 of the uninstall.exe Setup writes from this file (Payload.WriteStub).
    Get-ArkSetupRangeHash $Path 0 $Payload.Offset $Payload
}

Export-ModuleMember -Function Get-ArkPeLayout, Set-ArkPeField, New-ArkSetupFooter, Find-ArkSetupPayload, Test-ArkSetupFile, Get-ArkSetupPayloadHash, Get-ArkSetupStubHash
