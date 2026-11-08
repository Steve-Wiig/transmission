$i = 0
$lines = Get-Content qt/PrefsDialog.ui
$output = @()
foreach ($line in $lines) {
    $i++
    if ($i -ge 917 -and $i -le 1107) {
        $output += " " + $line
    } else {
        $output += $line
    }
}
$output | Set-Content qt/PrefsDialog.ui