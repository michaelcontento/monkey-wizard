Strict

Private

#REFLECTION_FILTER+="wizard.commands.*"

Import os
Import reflection
Import wizard.android
Import wizard.command
Import wizard.commands
Import wizard.dir
Import wizard.file
Import wizard.helperos
Import wizard.ios

Public

Class App
    Private

    Field commands:StringMap<ClassInfo> = New StringMap<ClassInfo>()
    Field openFiles:StringMap<File> = New StringMap<File>()

    Public

    Method New()
        LoadPatchCommands()
        CheckNumberOfArguments()
        CheckTargetDirExists()

        If GetCommand()
            Ios.app = Self
            Android.app = Self

            ExecuteCommand(GetCommand())
            SaveOpenFiles()
        Else
            PrintInvalidCommandError(GetCommandRaw())
        End
    End

    Method TargetFile:File(filename:String)
        If Not openFiles.Contains(filename)
            openFiles.Set(filename, New File(GetTargetDir() + filename))
        End

        Return openFiles.Get(filename)
    End

    Method SourceFile:File(path:String)
        Return New File(GetCommandDataDir() + path)
    End

    Method TargetDir:Dir(path:String)
        Return New Dir(GetTargetDir() + path)
    End

    Method SourceDir:Dir(path:String)
        Return New Dir(GetCommandDataDir() + path)
    End

    Method PrintHelp:Void()
        Print "Usage: wizard COMMAND TARGETDIR [COMMAND SPECIFIC OPTIONS]"
        Print ""

        Print "Commands:"
        For Local command:String = EachIn commands.Keys()
            Print "  " + command
        End
    End

    Method LogInfo:Void(text:String)
        Print "INFO: " + text
    End

    Method LogWarning:Void(text:String)
        Print "WARN: " + text
    End

    Method LogError:Void(text:String)
        Print "ERRO: " + text
        ExitApp(2)
    End

    Method GetAdditionArguments:String[]()
        Return AppArgs()[3..]
    End

    Private

    Method SaveOpenFiles:Void()
        For Local f:File = EachIn openFiles.Values()
            f.Save()
        End
    End

    Method FixCase:String(command:String)
        For Local checkCommand:String = EachIn commands.Keys()
            If checkCommand.ToLower() = command.ToLower()
                Return checkCommand
            End
        End

        Return ""
    End

    Method PrintInvalidCommandError:Void(command:String)
        PrintHelp()
        Print ""
        LogError(command + " is not a avalid command")
    End

    Method ExecuteCommand:Void(command:String)
        Local info:ClassInfo = commands.Get(command)
        Local obj:Command = Command(info.NewInstance())
        obj.Run(Self)
    End

    Method LoadPatchCommands:Void()
        For Local classInfo:ClassInfo = EachIn GetClasses()
            If classInfo.Name().Contains("wizard.commands.")
                commands.Add(GetShortName(classInfo.Name()), classInfo)
            End
        End
    End

    Method GetShortName:String(longName:String)
        Local lastPart:String = ""
        For Local parts:String = EachIn longName.Split(".")
            lastPart = parts
        End
        Return lastPart
    End

    Method CheckNumberOfArguments:Void()
        If AppArgs().Length() <= 2
            PrintHelp()
            Print ""
            LogError("Invalid number of arguments")
            ExitApp(2)
        End
    End

    Method CheckTargetDirExists:Void()
        If Not DirExists(GetTargetDir())
            LogError("Given targetdir " + GetTargetDir() + " does not exists")
        End
    End

    Method GetCommand:String()
        Return FixCase(GetCommandRaw())
    End

    Method GetCommandRaw:String()
        Return AppArgs()[1]
    End

    Method GetTargetDir:String()
        Return AppArgs()[2] + "/"
    End

    Method GetCommandDataDir:String()
        Local baseDir:String = RealPath(ExtractDir(AppPath()) + "/../../") + "/"
        Local dataDir:String = baseDir + "wizard.data/commands/"
        Return dataDir + GetCommandRaw().ToLower() + "/"
    End
End
