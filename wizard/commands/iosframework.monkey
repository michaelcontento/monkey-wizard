Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosFramework Implements Command
    Method Run:Void(app:App)
        If app.GetAdditionArguments().Length() <> 2
            app.LogInfo("First argument must be the name of the framework")
            app.LogInfo("Second argument indicates (0/1) if it's optional")
            app.LogError("Invalid number of arguments given")
        End

        Local name:String = app.GetAdditionArguments()[0]
        Local optional:Bool = (app.GetAdditionArguments()[1] = "1")
        Ios.AddFramework(name, optional)
    End
End
