param([string]$SourceRoot=(Join-Path (Split-Path $PSScriptRoot -Parent) 'Source\CreatureFrontier'))
$ErrorActionPreference='Stop'
if(!(Test-Path -LiteralPath $SourceRoot -PathType Container)){throw '找不到待检查的源码目录'}

# 只读补充检查；资产另用audit_chinese.py核对，截图仍须人工检查字体与排版。
# 按键标识和字符编码名称属于技术文字，不强行翻译，也不改玩家昵称。
$allowed=@('WASD','Shift','Tab','Insert','UTF-16')
$failures=@()
$checked=0
foreach($file in Get-ChildItem -LiteralPath $SourceRoot -File | Where-Object Extension -In '.cpp','.h'){
    $lineNumber=0
    foreach($line in Get-Content -LiteralPath $file.FullName -Encoding UTF8){
        $lineNumber++
        foreach($literal in [regex]::Matches($line,'TEXT\("((?:\\.|[^"\\])*)"\)')){
            $value=$literal.Groups[1].Value
            if($value -match '[\u4e00-\u9fff]'){
                $checked++
                $words=$value -replace '\\[nrt]',' ' -replace '%[-+0 #]*\d*(?:\.\d+)?[sdfui]',' '
                foreach($token in [regex]::Matches($words,'[A-Za-z][A-Za-z0-9_-]*')){
                    $word=$token.Value
                    if($word.Length -gt 1 -and $word -notmatch '^F\d{1,2}$' -and $word -cnotin $allowed){
                        $failures+=('{0}:{1} 展示文字中有未核准英文：{2}' -f $file.FullName,$lineNumber,$word)
                    }
                }
            }elseif($file.Name -notmatch 'Scenario|Preview|HollowAdventure' -and
                $line -match 'Feedback\s*=|DrawText\(|FText::FromString\(\s*TEXT\(' -and
                $value -match '[A-Za-z]{2,}' -and $value -notmatch '^CF_'){
                $failures+=('{0}:{1} 直接展示文字缺少中文：{2}' -f $file.FullName,$lineNumber,$value)
            }
        }
    }
}
if($failures.Count){$failures | ForEach-Object {Write-Output $_};throw ('中文源码检查失败，共'+$failures.Count+'处')}
if(!$checked){throw '没有找到中文展示字符串，不能视作检查通过'}
Write-Host ('中文源码检查通过：'+$checked+'条含中文的文字；直接展示入口未发现纯英文文案。此静态抽样不替代资产、动态拼接或图形验收。')
