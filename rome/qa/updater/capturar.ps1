# Foto de una ventana con PrintWindow (PW_RENDERFULLCONTENT): no la trae al frente ni toca el ratón.
#   powershell -File capturar.ps1 -Ventana <hwnd> -Salida foto.png [-Max 640]
param(
	[Parameter(Mandatory = $true)][long]$Ventana,
	[Parameter(Mandatory = $true)][string]$Salida,
	[int]$Max = 640
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class FotoVentana {
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
	public struct RECT { public int L; public int T; public int R; public int B; }
}
"@
$manejador = [IntPtr]$Ventana
$caja = New-Object FotoVentana+RECT
[FotoVentana]::GetWindowRect($manejador, [ref]$caja) | Out-Null
$ancho = $caja.R - $caja.L
$alto = $caja.B - $caja.T
if ($ancho -le 0 -or $alto -le 0) {
	"sin ventana"
	exit 1
}
$foto = New-Object System.Drawing.Bitmap $ancho, $alto
$g = [System.Drawing.Graphics]::FromImage($foto)
$dc = $g.GetHdc()
[FotoVentana]::PrintWindow($manejador, $dc, 2) | Out-Null
$g.ReleaseHdc($dc)
$g.Dispose()
$escala = [Math]::Min(1.0, $Max / [double][Math]::Max($ancho, $alto))
$chica = New-Object System.Drawing.Bitmap ([Math]::Max(1, [int]($ancho * $escala))), ([Math]::Max(1, [int]($alto * $escala)))
$g = [System.Drawing.Graphics]::FromImage($chica)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($foto, 0, 0, $chica.Width, $chica.Height)
$g.Dispose()
$chica.Save($Salida, [System.Drawing.Imaging.ImageFormat]::Png)
"ok $ancho x $alto -> $($chica.Width) x $($chica.Height)"
$foto.Dispose()
$chica.Dispose()
