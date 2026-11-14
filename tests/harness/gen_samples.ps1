param([string]$Dir)
# big.md: ~20 MB of normal markdown; longline.md: 1 MB single line; small.md: rich sample
$sb = New-Object System.Text.StringBuilder
$i = 0
while ($sb.Length -lt 20MB) {
    [void]$sb.Append("## Section $i`n`nSome **bold**, *italic*, ``code`` and a [link](http://x/$i).`n`n- a`n- b`n`n| a | b |`n|---|---|`n| 1 | 2 |`n`n")
    $i++
}
[IO.File]::WriteAllText("$Dir\big.md", $sb.ToString())
[IO.File]::WriteAllText("$Dir\longline.md", ("word **b** *i* " * 70000))
[IO.File]::WriteAllText("$Dir\small.md", "# Hello`n`nThis is **bold** and *italic* with ``code``.`n`n| a | b |`n|---|---|`n| 1 | 2 |`n`n- one`n- two`n")
Get-ChildItem $Dir\*.md | Select-Object Name, Length
