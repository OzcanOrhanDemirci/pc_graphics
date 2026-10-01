# Captures the primary monitor to a PNG: what is actually on the screen, not what the renderer wrote.
param([string]$Out = "screen.png", [double]$DelaySeconds = 0)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
if ($DelaySeconds -gt 0) { Start-Sleep -Milliseconds ([int]($DelaySeconds * 1000)) }
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "$($b.Width)x$($b.Height) -> $Out"
