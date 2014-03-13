Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class AndroidChartboost Implements Command
    Const VERSION:String = "3.4.0"

    Method Run:Void(app:App)
        Android.AddPermission("android.permission.WRITE_EXTERNAL_STORAGE")
        Android.AddPermission("android.permission.ACCESS_NETWORK_STATE")
        Android.AddPermission("android.permission.ACCESS_WIFI_STATE")
        Android.AddPermission("android.permission.INTERNET")

        CopyLibs(app)
    End

    Private

    Method CopyLibs:Void(app:App)
        Android.EnsureLibsFolder()

        Local src:File = app.SourceFile("Chartboost-" + VERSION + "/chartboost.jar")
        Local dst:File = app.TargetFile("libs/chartboost-" + VERSION + ".jar")

        src.CopyTo(dst)
    End
End
