Strict

Import wizard.app
Import wizard.ios
Import os

Class CocoaPods
    Function Exec:Void(cmd$, app:App)
        Local execute := "cd ~q" + app.TargetDir("").GetPath() + "~q"
        execute += " && export LANG=en_US.UTF-8"
        execute += " && ~~/.rbenv/shims/pod " + cmd
        If Execute(execute) <> 0 Then Error "ERROR: pod " + cmd
    End

    Function AddDependency:Void(dependency$, app:App)
        Local pod := app.TargetFile("Podfile")
        dependency = "~t" + "pod ~q" + dependency + "~q" + "~n"
        If (Not pod.Contains(dependency))
            pod.InsertAfter("target 'MonkeyGame' do", "~n" +dependency)
            pod.Save()
        End
    End

    Function Init:Void(app:App)
        Ios.EnsureHeaderSearchPath("~q$(inherited)~q")
        Ios.EnsureOtherLDFlags("~q$(inherited)~q")
        If (Not app.TargetFile("Podfile").Exists())
            CocoaPods.Exec("init", app)
        End
    End

    Function Install:Void(app:App)
        CocoaPods.Exec("install", app)
    End
End