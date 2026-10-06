param(
    [Parameter(Mandatory = $true)]
    [string]$CommandPath,

    [string[]]$Arguments = @()
)

$ErrorActionPreference = "Stop"

& "$PSScriptRoot\use_fixed_env.ps1"
& $CommandPath @Arguments
exit $LASTEXITCODE
