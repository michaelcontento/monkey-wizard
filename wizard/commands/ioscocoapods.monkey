Strict

Private

Import wizard.app
Import wizard.command
Import wizard.cocoapods

Public

Class IosCocoapods Implements Command
    Method Run:Void(app:App)
        CocoaPods.Init(app)
        CocoaPods.Install(app)
    End
End
