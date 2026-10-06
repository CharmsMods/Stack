param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,

    [string]$StackExecutable = "build\Stack.exe",

    [switch]$ForceCpu
)

throw @"
Restormer package execution is disabled for this runtime-layout pass.
Normal builds ignore STACK_RESTORMER_DENOISE_DIR,
STACK_ALLOW_LOCAL_RESTORMER_PACKAGE, and STACK_RESTORMER_FORCE_CPU.
Use Classical Multiscale in Stack. A future private build must deliberately
enable the compile-time gate before this launcher can be restored.
"@
