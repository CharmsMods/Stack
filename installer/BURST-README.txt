Stack Burst

This package installs the same StackBurst.exe processing component used by the
full Stack application, together with its required RAW runtime and notices.

Open Stack Burst from its shortcut to select RAW captures, or select captures
in File Explorer, right-click, and choose "Burst denoise with Stack". On Windows
11 the command is under "Show more options". Both installers automatically enable integration
on native x64 Windows. Portable ZIPs do not register it.

The window analyzes only the selected files and groups compatible bursts using
Stack's Gallery grouping rules. Choose output formats and whether results go
beside the source files or into a new subfolder. The window reports progress and
allows cancellation. Original camera files remain read-only.

Output formats are editable DNG, 16-bit TIFF, JPEG, and PNG. DNG output contains
the processed Bayer mosaic rather than a rendered preview. TIFF, JPEG, and PNG
use the same default tone, gamut and display transform as Stack. Authored edits
in a Stack project can intentionally change that finish. Supported input extensions are .arw, .srf, .sr2, .raw, and
.dng; the engine checks each capture's supported sensor layout and metadata.
Unsupported captures and sets that cannot form a compatible burst are reported.

Command-line entry points from this installation folder:
  .\StackBurst.exe --files <RAW> <RAW> [RAW ...]
  .\StackBurst.exe --analyze <manifest.json>
  .\StackBurst.exe --batch <manifest.json>

The UTF-8 manifest uses schemaVersion 1 and an array named files containing
absolute input paths. Batch jobs also accept outputFormats (an array of dng,
tiff, jpeg, and/or png) and destination (beside or folder). Example:
  {"schemaVersion":1,"files":["C:\\Photos\\01.ARW","C:\\Photos\\02.ARW"],
   "outputFormats":["dng","tiff","jpeg","png"],"destination":"folder"}

Diagnostic commands:
  .\StackBurst.exe --version
  .\StackBurst.exe --self-check
  .\StackBurst.exe --process <new-output-directory> <RAW> <RAW> [RAW ...]

The --process diagnostic command treats the supplied RAW files as one explicit
denoise group and writes an internal result.bracket artifact in a new output
directory. Use the normal window or --batch for grouping and finished outputs.
For scripted checks, explicitly wait for this GUI executable and collect its
exit code/output; launching it from a console alone may return immediately.

The --worker interface is an internal, versioned interface used by Stack. Its
handle arguments are supplied by the calling application; do not fabricate them.

Full Stack and Stack Burst use separate installation folders and uninstall
identities. If both are installed, each owns a private copy of the same engine
binary, so removing one product does not break the other. Full Stack uses only
the engine in its own installation; it does not discover another installation.
The Explorer command appears once when both products are installed. It prefers
standalone Stack Burst, then full Stack, then isolated local-test installations.
Uninstalling the active provider redirects the command to a remaining provider.

The current Explorer adapter is native x64. On ARM64 Windows the x64 application
may run through supported emulation, but this package does not register an
ARM64 Explorer extension. Use the standalone window instead. Modern Windows 11
top-level menu integration requires a signed identity package and is not included.
An Explorer-loaded adapter may require a Windows restart after upgrade/uninstall;
the installer reports when replacement or removal has been scheduled.

This product includes DNG technology under license by Adobe.
See Stack/App/Legal/Adobe-DNG-Notice.txt for the source of this notice.
