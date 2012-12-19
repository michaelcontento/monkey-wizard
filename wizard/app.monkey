Strict

Private

#REFLECTION_FILTER+="wizard.commands.*"

Import os
Import reflection
Import wizard.command
Import wizard.commands
Import wizard.file

Public

Class App
    Private

    Field commands:StringMap<ClassInfo> = New StringMap<ClassInfo>()
    Field openFiles:StringMap<File> = New StringMap<File>()

    Public

    Method New()
        LoadPatchCommands()
        CheckNumberOfArguments()

        If GetCommand()
            ExecuteCommand(GetCommand())
            SaveOpenFiles()
        Else
            PrintInvalidCommandError(GetCommandRaw())
        End
    End

    Method TargetFile:File(filename:String)
        If Not openFiles.Contains(filename)
            openFiles.Set(filename, New File(filename))
        End

        Return openFiles.Get(filename)
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

    Method GetCommand:String()
        Return FixCase(GetCommandRaw())
    End

    Method GetCommandRaw:String()
        Return AppArgs()[1]
    End

    Method GetTargetDir:String()
        Return AppArgs()[2]
    End
End
