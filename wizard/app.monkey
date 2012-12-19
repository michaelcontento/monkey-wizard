Strict

Private

#REFLECTION_FILTER+="wizard.commands.*"

Import os
Import reflection
Import command
Import commands

Public

Class App
    Private

    Field commands:StringMap<ClassInfo> = New StringMap<ClassInfo>

    Public

    Method New()
        LoadPatchCommands()
        Local rawCommand:String = GetCommand()
        Local fixedCommand:String = FixCase(rawCommand)

        If fixedCommand
            ExecuteCommand(fixedCommand)
        Else
            PrintInvalidCommandError(rawCommand)
        End
    End

    Method PrintHelp:Void()
        Print "Usage: wizard COMMAND [COMMAND SPECIFIC OPTIONS]"
        Print ""

        Print "Commands:"
        For Local command:String = EachIn commands.Keys()
            Print "  " + command
        End
    End

    Private

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

        Print "Error:"
        Print "  " + command + " is not a valid command."

        ExitApp(2)
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

    Method GetCommand:String()
        If AppArgs().Length() = 1
            PrintHelp()
            ExitApp(2)
        End

        Return AppArgs()[1]
    End
End
