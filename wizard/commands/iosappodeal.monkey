Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios
Import wizard.cocoapods

Public

Class IosAppodeal Implements Command
    Method Run:Void(app:App)
        Ios.NSAllowsArbitraryLoads()
        CocoaPods.Init(app)
        'CocoaPods.AddSource("https://github.com/appodeal/CocoaPods.git", app)
        'CocoaPods.AddSource("https://github.com/CocoaPods/Specs.git", app)
        CocoaPods.AddDependency("Appodeal", app, "1.3.9")
        'CocoaPods.RepoUpdate(app)
        CocoaPods.Install(app)

        Ios.AddToPlist("NSBluetoothPeripheralUsageDescription", "Advertising")
        Ios.AddToPlist("NSCalendarsUsageDescription", "Advertising")
        Ios.AddToPlist("NSLocationWhenInUseUsageDescription", "Advertising")
        Ios.AddToPlist("NSPhotoLibraryUsageDescription", "Advertising")
    End
End
