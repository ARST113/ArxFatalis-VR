param(
    [Parameter(Mandatory)][string]$ExtractedDirectory,
    [string]$Manifest = (Join-Path $PSScriptRoot 'files-sha256.csv')
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ExtractedDirectory).Path.TrimEnd('\','/')
$prefix = $root + [IO.Path]::DirectorySeparatorChar
$records = @(Import-Csv -LiteralPath $Manifest)
$failures = [Collections.Generic.List[string]]::new()
foreach($record in $records) {
    $target = [IO.Path]::GetFullPath((Join-Path $root $record.path))
    if(!$target.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escapes snapshot root' }
    if(!(Test-Path -LiteralPath $target -PathType Leaf)) { $failures.Add("Missing: $($record.path)"); continue }
    if((Get-Item -LiteralPath $target).Length -ne [long]$record.bytes) { $failures.Add("Size: $($record.path)"); continue }
    if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $record.sha256) { $failures.Add("SHA256: $($record.path)") }
}
if($failures.Count) {
    $failures | Write-Output
    throw "$($failures.Count) snapshot verification failures"
}
Write-Output "PASS: $($records.Count) files match manifest sizes and SHA-256 hashes."
