function Get-VerifiedDownload {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][Uri] $Uri,
        [Parameter(Mandatory)][string] $Destination,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string] $Sha256,
        [ValidateRange(1, 10)][int] $MaxAttempts = 4,
        [ValidateRange(0, 60)][int] $RetryDelaySeconds = 2
    )

    $directory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $directory | Out-Null

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
        if ($hash.Equals($Sha256, [StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "Using cached dependency: $(Split-Path -Leaf $Destination)"
            return
        }
        Remove-Item -Force -LiteralPath $Destination
    }

    $partial = "$Destination.partial"
    for ($attempt = 1; $attempt -le $MaxAttempts; ++$attempt) {
        Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $partial
        Write-Host "Downloading $Uri (attempt $attempt of $MaxAttempts)..."
        & curl.exe --fail --location --silent --show-error --output $partial $Uri.AbsoluteUri
        $exitCode = $LASTEXITCODE

        if ($exitCode -eq 0 -and (Test-Path -LiteralPath $partial -PathType Leaf)) {
            $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $partial).Hash
            if ($actual.Equals($Sha256, [StringComparison]::OrdinalIgnoreCase)) {
                Move-Item -Force -LiteralPath $partial -Destination $Destination
                Write-Host "Downloaded and verified: $(Split-Path -Leaf $Destination)"
                return
            }
            Write-Warning "Unexpected SHA-256 hash."
        }

        if ($attempt -lt $MaxAttempts) {
            Start-Sleep -Seconds ([Math]::Min($RetryDelaySeconds * [Math]::Pow(2, $attempt - 1), 10))
        }
    }

    Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $partial
    throw "Unable to download and verify '$Uri'."
}
