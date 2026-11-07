using System.Globalization;

namespace ArkSetup.Core;

/// <summary>
/// The installer's text: English by default, European Portuguese (pt-PT) when Windows is in Portuguese.
/// --lang en|pt forces one.
/// </summary>
public static class S
{
    public static bool Pt { get; private set; } =
        CultureInfo.CurrentUICulture.TwoLetterISOLanguageName.Equals("pt", StringComparison.OrdinalIgnoreCase);

    public static void Force(string? lang)
    {
        if (lang == "pt") Pt = true;
        else if (lang == "en") Pt = false;
    }

    static string T(string en, string pt) => Pt ? pt : en;

    // window
    public static string WindowTitle => T("ARK Setup", "Instalação do ARK");
    public static string SetupLabel => T("Setup", "Instalação");
    public static string Minimize => T("Minimise", "Minimizar");
    public static string Close => T("Close", "Fechar");

    // welcome
    public static string Tagline => T("Parametric CAD for people and AI agents.",
                                      "CAD paramétrico para pessoas\ne agentes de IA.");
    public static string Blurb => T("Installs for this Windows user only.\nNo administrator rights needed.",
                                    "Instala apenas para este utilizador do Windows.\nNão precisa de permissões de administrador.");
    public static string Install => T("Install", "Instalar");
    public static string UpdateButton => T("Update", "Atualizar");
    public static string Reinstall => T("Reinstall", "Reinstalar");
    public static string Options => T("Options", "Opções");
    public static string InstallTo => T("Install to", "Instalar em");
    public static string InstalledVersion(string v) => T($"Installed: {v}", $"Instalado: {v}");

    // options
    public static string OptionsTitle => T("Install options", "Opções de instalação");
    public static string OptionsSub => T("Choose where ARK goes.", "Escolha onde instalar o ARK.");
    public static string Folder => T("Folder", "Pasta");
    public static string Browse => T("Browse…", "Procurar…");
    public static string BrowseTitle => T("Choose the install folder", "Escolha a pasta de instalação");
    public static string DesktopShortcut => T("Desktop shortcut", "Atalho no ambiente de trabalho");
    public static string StartMenuShortcut => T("Start menu shortcut", "Atalho no menu Iniciar");
    public static string SpaceRequired => T("Space required", "Espaço necessário");
    public static string SpaceAvailable => T("Available", "Disponível");
    public static string NotEnoughSpace => T("Not enough free space on this drive.", "Não há espaço livre suficiente neste disco.");
    public static string InvalidFolder => T("Choose a valid folder.", "Escolha uma pasta válida.");
    public static string DataNote => T("Your drawings and settings are kept outside this folder.",
                                       "Os seus desenhos e definições ficam fora desta pasta.");
    public static string Back => T("Back", "Voltar");

    // installing
    public static string Installing => T("Installing ARK", "A instalar o ARK");
    public static string Extracting => T("Extracting files…", "A extrair ficheiros…");
    public static string Preparing => T("Preparing ARK…", "A preparar o ARK…");
    public static string Finishing => T("Finishing…", "A concluir…");
    public static string OfSize(string done, string total) => T($"{done} of {total}", $"{done} de {total}");
    public static string QuietLine => T("Your drawings and settings stay where they are.",
                                        "Os seus desenhos e definições ficam onde estão.");

    // done / error
    public static string ReadyTitle => T("ARK is ready", "O ARK está pronto");
    public static string InstalledLine(string title) => T($"{title} is installed.", $"O {title} está instalado.");
    public static string ShortcutsMade => T("Open it from the desktop or the Start menu.", "Abra-o no ambiente de trabalho ou no menu Iniciar.");
    public static string ShortcutDesktopOnly => T("Open it from the desktop.", "Abra-o no ambiente de trabalho.");
    public static string ShortcutStartOnly => T("Open it from the Start menu.", "Abra-o no menu Iniciar.");
    public static string NoShortcuts => T("Open it with ARK.cmd in the install folder.", "Abra-o com o ARK.cmd na pasta de instalação.");
    public static string OpenArk => T("Open ARK", "Abrir ARK");
    public static string FailedTitle => T("Installation failed", "A instalação falhou");
    public static string OpenLog => T("Open the log", "Abrir o registo");
    public static string TryAgain => T("Try again", "Tentar de novo");
    public static string ArkRunning => T("ARK is open. Close it and try again.",
                                         "O ARK está aberto. Feche-o e tente de novo.");
    public static string ArkRunningProcess(ArkRunningProcess process)
    {
        string identity = $"{process.Name} (PID {process.Pid})";
        if (process.Background && process.Name.Equals("ark-mcp.exe", StringComparison.OrdinalIgnoreCase))
            return T($"An ARK MCP session is still running in the background: {identity}.\nSave its work and disconnect the ARK session in the client application, then try again.",
                     $"Uma sessão MCP do ARK continua em segundo plano: {identity}.\nGuarde o trabalho e desligue a sessão ARK na aplicação cliente; depois tente de novo.");
        if (process.Background)
            return T($"An ARK background task is still running: {identity}.\nLet it finish or close it from its client application, then try again.",
                     $"Uma tarefa do ARK continua em segundo plano: {identity}.\nAguarde que termine ou feche-a pela aplicação cliente; depois tente de novo.");
        return T($"ARK is still running: {identity}.\nSave your drawings, close that instance and try again.",
                 $"O ARK continua em execução: {identity}.\nGuarde os desenhos, feche essa instância e tente de novo.");
    }
    public static string NoPayload => T("This setup file is incomplete. Download it again.",
                                        "Este ficheiro de instalação está incompleto. Volte a descarregá-lo.");
    public static string BadPayload(string detail) => T($"The setup data is damaged ({detail}). Download it again.",
                                                        $"Os dados da instalação estão danificados ({detail}). Volte a descarregá-los.");
    public static string FolderRefused(string why) => T($"ARK cannot be installed in this folder: {why}.",
                                                        $"Não é possível instalar o ARK nesta pasta: {why}.");
    public static string PowerShellMissing => T("Windows PowerShell could not be started.",
                                                "Não foi possível iniciar o Windows PowerShell.");
    public static string PrepareFailed(int code) => T($"ARK could not be prepared (code {code}).",
                                                      $"Não foi possível preparar o ARK (código {code}).");
    public static string PrepareTimedOut => T("Preparing ARK took too long and was stopped.",
                                              "A preparação do ARK demorou demasiado e foi interrompida.");
    public static string PrepareIncomplete => T("ARK was prepared, but the installed files are incomplete.",
                                                "O ARK foi preparado, mas os ficheiros instalados estão incompletos.");
    public static string FileLocked(string f) => T($"Could not write {f}.", $"Não foi possível escrever {f}.");
    public static string LaunchFailed => T("ARK could not be opened.", "Não foi possível abrir o ARK.");

    // uninstall
    public static string UninstallTitle => T("Uninstall ARK", "Desinstalar o ARK");
    public static string UninstallBody(string dir) => T($"This removes ARK from {dir}.", $"Isto remove o ARK de {dir}.");
    public static string UninstallKeeps => T("Drawings and settings are kept.", "Os desenhos e as definições são mantidos.");
    public static string UninstallButton => T("Uninstall", "Desinstalar");
    public static string Cancel => T("Cancel", "Cancelar");
    public static string Removing => T("Removing ARK…", "A remover o ARK…");
    public static string Removed => T("ARK was removed", "O ARK foi removido");
    public static string RemovedKept => T("Some other files were left in the folder.", "Ficaram outros ficheiros na pasta.");
    public static string UninstallFailedTitle => T("Uninstall failed", "A desinstalação falhou");
    public static string FilesInUse => T("Some ARK files are in use. Close ARK and try again.",
                                         "Alguns ficheiros do ARK estão em uso. Feche o ARK e tente de novo.");
    public static string NotInstalledHere => T("ARK is not installed in this folder, so nothing was removed.",
                                               "O ARK não está instalado nesta pasta, por isso nada foi removido.");
}
