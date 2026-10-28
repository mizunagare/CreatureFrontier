# 进程刚退出时，系统仍可能短暂占用程序文件；只重试读取，不重跑测试。
function Get-FrontierBuildFingerprint {
    param([Parameter(Mandatory=$true)][string]$LiteralPath)
    for($attempt=1;$attempt -le 8;$attempt++){
        try{return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256 -ErrorAction Stop).Hash}
        catch [System.IO.IOException]{
            if($attempt -eq 8){throw}
            if($attempt -eq 1){Write-Host '程序指纹暂时无法读取，正在进行有界重试'}
            Start-Sleep -Milliseconds 250
        }
    }
}
