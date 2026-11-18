<#
.SYNOPSIS
  Grava um arquivo num disco fisico por numero, com TRAVA de tamanho e confirmacao.
  Unico ponto do projeto que escreve em disco fisico.

.DESCRIPTION
  Marco 1: grava o stub no setor 16 (offset 8KB) do SD. Recusa o disco 0 (sistema)
  e qualquer disco maior que -MaxBytes (default 2GB, a trava do Marco 1). Pede
  confirmacao digitada ("SIM") e, por padrao, le de volta pra conferir.
  Requer PowerShell como Administrador. Offset e tamanho sao multiplos de 512.

.EXAMPLE
  .\write_sd.ps1 -DiskNumber 2 -File ..\build\marco1_stub.bin
#>
#Requires -RunAsAdministrator
param(
  [Parameter(Mandatory=$true)][int]$DiskNumber,
  [Parameter(Mandatory=$true)][string]$File,
  [long]$Offset = 8192,            # setor 16 (512B) = 8KB, onde a BootROM procura o boot0
  [long]$MaxBytes = 2147483648,    # trava Marco 1: recusa disco > 2GB
  [switch]$NoVerify
)
$ErrorActionPreference = "Stop"

if ($DiskNumber -eq 0) { throw "RECUSADO: disco 0 e o sistema (NVMe do PC). Nunca." }
if (-not (Test-Path $File)) { throw "Arquivo nao encontrado: $File" }

$disk = Get-Disk -Number $DiskNumber -ErrorAction SilentlyContinue
if (-not $disk -or $disk.Size -le 0) { throw "RECUSADO: disco $DiskNumber nao encontrado ou sem midia (cartao no leitor? confira Get-Disk)." }
$sizeGB = [math]::Round($disk.Size / 1GB, 2)
if ($disk.Size -gt $MaxBytes) {
  throw "RECUSADO: disco $DiskNumber tem $sizeGB GB (> trava de $([math]::Round($MaxBytes/1GB,2)) GB). Marco 1 so grava no SD de 512MB."
}

$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $File))
if ($bytes.Length % 512 -ne 0) { throw "RECUSADO: arquivo tem $($bytes.Length) bytes (nao multiplo de 512; escrita em disco cru exige alinhamento de setor)." }
if ($Offset % 512 -ne 0)       { throw "RECUSADO: offset $Offset nao e multiplo de 512." }

Write-Host "Disco $DiskNumber : $($disk.FriendlyName)  $sizeGB GB  (bus $($disk.BusType))"
Write-Host "Vai gravar $($bytes.Length) bytes de '$File' no offset $Offset (setor $($Offset/512))."
$ans = Read-Host "Confirme digitando  SIM  (qualquer outra coisa cancela)"
if ($ans -ne "SIM") { Write-Host "Cancelado."; exit 1 }

$path = "\\.\PhysicalDrive$DiskNumber"
$fs = New-Object System.IO.FileStream($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::ReadWrite)
try {
  $fs.Seek($Offset, [System.IO.SeekOrigin]::Begin) | Out-Null
  $fs.Write($bytes, 0, $bytes.Length)
  $fs.Flush()
  if (-not $NoVerify) {
    $fs.Seek($Offset, [System.IO.SeekOrigin]::Begin) | Out-Null
    $back = New-Object byte[] $bytes.Length
    $read = $fs.Read($back, 0, $bytes.Length)
    $ok = ($read -eq $bytes.Length)
    for ($i = 0; $i -lt $bytes.Length -and $ok; $i++) { if ($back[$i] -ne $bytes[$i]) { $ok = $false } }
    if (-not $ok) { throw "VERIFICACAO FALHOU: leitura nao bate com o gravado." }
    Write-Host "OK: gravado e verificado ($($bytes.Length) bytes)."
  } else {
    Write-Host "OK: gravado ($($bytes.Length) bytes). Verificacao pulada."
  }
} finally { $fs.Close() }
