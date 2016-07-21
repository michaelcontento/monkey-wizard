Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios
Import wizard.cocoapods

Public

Class IosChartboost Implements Command
    Method Run:Void(app:App)
        Ios.NSAllowsArbitraryLoads()
        CocoaPods.Init(app)
        CocoaPods.AddDependency("ChartboostSDK", app)
        CocoaPods.Install(app)

        app.LogInfo("Chartboost installed!")
    End
End
