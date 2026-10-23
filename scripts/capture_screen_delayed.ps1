# capture_screen_delayed.ps1 -- screenshot a menu that would close on focus change.
#
# Snipping Tool activates itself, and Windows dismisses a context/column menu as
# soon as focus moves -- so the menu is gone before the snip starts.  This
# script sleeps for N seconds, then grabs the primary screen with GDI from a
# background process that never takes the foreground.  Start it, switch to
# Project, open the menu, and wait: the capture happens with the menu still up.
#
#   powershell -ExecutionPolicy Bypass -File scripts\capture_screen_delayed.ps1 -Seconds 6 -Out C:\tmp\colmenu.png
#
# Print Screen + Paste into Paint also works (PrtScn is a system key and does not
# steal focus); this script is for when you want a file without the clipboard.
param(
    [int]$Seconds = 6,
    [string]$Out = "screenshot.png"
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Start-Sleep -Seconds $Seconds

$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
$bitmap.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()

Write-Output "wrote $Out ($($bounds.Width)x$($bounds.Height))"
