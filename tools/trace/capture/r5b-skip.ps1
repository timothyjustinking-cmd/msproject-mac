param([int]$ProcId, [int]$WaitSec = 60, [switch]$DumpOnly)
# Inside the VM.  Authorised for ONE control only (coordinator addendum, round 5b): the "Skip for now" button of Project's
# sign-in dialog.  Finds the NUIDialog of process $ProcId, invokes the element named exactly "Skip for now" and nothing else.
# Never touches "Sign in or create account" or "I have a product key"; types nothing.  Prints one status line.
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$t0 = Get-Date
$names = @()
while (((Get-Date) - $t0).TotalSeconds -lt $WaitSec) {
    $c1 = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $ProcId)
    $c2 = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ClassNameProperty, 'NUIDialog')
    $cond = New-Object System.Windows.Automation.AndCondition($c1, $c2)
    $dlg = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
    if (-not $dlg) {   # the dialog may be owned by / nested under the main window: look one level down inside the Project window
        $mw = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $c1)
        if ($mw) { $dlg = $mw.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $c2) }
    }
    if ($dlg) {
        $all = $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
        $names = @(); foreach ($e in $all) { $names += ($e.Current.ControlType.ProgrammaticName -replace 'ControlType\.', '') + ':' + $e.Current.Name }
        $skip = $null
        foreach ($e in $all) { if ($e.Current.Name -eq 'Skip for now') { $skip = $e; break } }
        if ($DumpOnly) { "dialog '" + $dlg.Current.Name + "' elements: " + (($names | ? { $_ -notmatch ':$' }) -join ' | '); return }
        if ($skip) {
            $ip = $null
            if ($skip.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke(); "SKIPPED dialog '" + $dlg.Current.Name + "'"; return }
            "NOINVOKE '" + $dlg.Current.Name + "' " + (($names | ? { $_ -notmatch ':$' }) -join ' | '); return
        }
        # dialog present but no Skip button (e.g. the licence-days page): report its text, touch nothing
        if ($dlg.Current.Name -notmatch 'Sign in') { "OTHERDIALOG '" + $dlg.Current.Name + "' " + (($names | ? { $_ -notmatch ':$' }) -join ' | '); return }
        # a sign-in dialog whose content has not drawn yet: keep waiting for the Skip link
    }
    Start-Sleep -Milliseconds 700
}
"NODIALOG"
