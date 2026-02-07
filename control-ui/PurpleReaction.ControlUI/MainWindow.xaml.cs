using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Windows.Storage;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace PurpleReaction.ControlUI;

public sealed partial class MainWindow : Window
{
    private readonly ObservableCollection<ReactionTrialResult> _trials = [];
    private ReactionRunResult? _lastResult;

    public MainWindow()
    {
        this.InitializeComponent();
        TrialsListView.ItemsSource = _trials;
        ExePathTextBox.Text = BuildDefaultExePath();
        VersionTextBlock.Text = $"Version: {BuildVersionString()}";
    }

    private static string BuildDefaultExePath()
    {
        string preferredConfig =
#if DEBUG
            "Debug";
#else
            "Release";
#endif
        string fallbackConfig = string.Equals(preferredConfig, "Debug", StringComparison.OrdinalIgnoreCase)
            ? "Release"
            : "Debug";

        foreach (string root in EnumerateCandidateRoots())
        {
            string preferredPath = Path.Combine(root, "build-vs18", preferredConfig, "PurpleReaction.exe");
            if (File.Exists(preferredPath))
            {
                return preferredPath;
            }

            string fallbackPath = Path.Combine(root, "build-vs18", fallbackConfig, "PurpleReaction.exe");
            if (File.Exists(fallbackPath))
            {
                return fallbackPath;
            }
        }

        return Path.GetFullPath(Path.Combine(Environment.CurrentDirectory, "build-vs18", preferredConfig, "PurpleReaction.exe"));
    }

    private static IEnumerable<string> EnumerateCandidateRoots()
    {
        HashSet<string> visited = new(StringComparer.OrdinalIgnoreCase);

        foreach (string seed in new[] { Environment.CurrentDirectory, AppContext.BaseDirectory })
        {
            string? current = Path.GetFullPath(seed);
            for (int i = 0; i < 10 && !string.IsNullOrEmpty(current); ++i)
            {
                if (visited.Add(current))
                {
                    yield return current;
                }

                DirectoryInfo? parent = Directory.GetParent(current);
                current = parent?.FullName;
            }
        }
    }

    private static string BuildVersionString()
    {
        Version? version = typeof(MainWindow).Assembly.GetName().Version;
        if (version is null)
        {
            return "Unknown";
        }

        int build = version.Build >= 0 ? version.Build : 0;
        return $"{version.Major}.{version.Minor}.{build}";
    }

    private static string BuildCsv(ReactionRunResult result)
    {
        StringBuilder builder = new();
        builder.AppendLine("trial,random_delay_seconds,reaction_ms,false_start");
        foreach (ReactionTrialResult trial in result.Trials)
        {
            builder.Append(trial.Trial.ToString(CultureInfo.InvariantCulture))
                .Append(',')
                .Append(trial.RandomDelaySeconds.ToString("F6", CultureInfo.InvariantCulture))
                .Append(',');

            if (!trial.FalseStart && trial.ReactionMs.HasValue)
            {
                builder.Append(trial.ReactionMs.Value.ToString("F6", CultureInfo.InvariantCulture));
            }

            builder.Append(',')
                .Append(trial.FalseStart ? "1" : "0")
                .AppendLine();
        }

        string averageText = result.AverageReactionMs.HasValue
            ? result.AverageReactionMs.Value.ToString("F6", CultureInfo.InvariantCulture)
            : string.Empty;
        builder.Append("average,,")
            .Append(averageText)
            .AppendLine(",");
        return builder.ToString();
    }

    private async void OnExportCsvClicked(object sender, RoutedEventArgs e)
    {
        if (_lastResult is null || _lastResult.Trials.Count == 0)
        {
            StatusTextBlock.Text = "No results available to export.";
            return;
        }

        try
        {
            FileSavePicker picker = new();
            picker.SuggestedStartLocation = PickerLocationId.DocumentsLibrary;
            picker.FileTypeChoices.Add("CSV file", new List<string> { ".csv" });
            picker.SuggestedFileName = $"PurpleReaction_{DateTime.Now:yyyyMMdd_HHmmss}";

            nint hwnd = WindowNative.GetWindowHandle(this);
            InitializeWithWindow.Initialize(picker, hwnd);

            StorageFile? target = await picker.PickSaveFileAsync();
            if (target is null)
            {
                StatusTextBlock.Text = "CSV export cancelled.";
                return;
            }

            string csv = BuildCsv(_lastResult);
            await FileIO.WriteTextAsync(target, csv);
            StatusTextBlock.Text = $"CSV exported: {target.Path}";
        }
        catch (Exception ex)
        {
            StatusTextBlock.Text = $"CSV export failed: {ex.Message}";
        }
    }

    private void OnTrialsListPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        TrialsListView.Focus(FocusState.Pointer);
    }

    private async void OnStartRunClicked(object sender, RoutedEventArgs e)
    {
        string exePath = ExePathTextBox.Text.Trim();
        if (!File.Exists(exePath))
        {
            StatusTextBlock.Text = "Executable path does not exist.";
            return;
        }

        if (!double.TryParse(MinDelayTextBox.Text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out double minDelay) ||
            !double.TryParse(MaxDelayTextBox.Text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out double maxDelay) ||
            !int.TryParse(TrialsTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int trials))
        {
            StatusTextBlock.Text = "Invalid settings values.";
            return;
        }

        if (minDelay <= 0.0 || maxDelay <= 0.0 || minDelay >= maxDelay || trials <= 0)
        {
            StatusTextBlock.Text = "Settings out of range. Ensure min > 0, max > min, trials > 0.";
            return;
        }

        string jsonPath = Path.Combine(
            Path.GetTempPath(),
            $"PurpleReaction_{DateTime.UtcNow:yyyyMMdd_HHmmss}_{Guid.NewGuid():N}.json");

        StartRunButton.IsEnabled = false;
        ExportCsvButton.IsEnabled = false;
        StatusTextBlock.Text = "Running fullscreen test...";

        try
        {
            ProcessStartInfo startInfo = new(exePath)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
            };

            startInfo.ArgumentList.Add("--run-once");
            startInfo.ArgumentList.Add("--min-delay");
            startInfo.ArgumentList.Add(minDelay.ToString(CultureInfo.InvariantCulture));
            startInfo.ArgumentList.Add("--max-delay");
            startInfo.ArgumentList.Add(maxDelay.ToString(CultureInfo.InvariantCulture));
            startInfo.ArgumentList.Add("--trials");
            startInfo.ArgumentList.Add(trials.ToString(CultureInfo.InvariantCulture));
            startInfo.ArgumentList.Add("--json-out");
            startInfo.ArgumentList.Add(jsonPath);

            using Process process = new() { StartInfo = startInfo };
            if (!process.Start())
            {
                StatusTextBlock.Text = "Failed to start test process.";
                return;
            }

            await process.WaitForExitAsync();
            if (process.ExitCode != 0)
            {
                StatusTextBlock.Text = $"Test process exited with code {process.ExitCode}.";
                return;
            }

            if (!File.Exists(jsonPath))
            {
                StatusTextBlock.Text = "Run completed but JSON output is missing.";
                return;
            }

            string json = await File.ReadAllTextAsync(jsonPath);
            ReactionRunResult? result = JsonSerializer.Deserialize<ReactionRunResult>(json);
            if (result is null)
            {
                StatusTextBlock.Text = "Failed to parse JSON output.";
                return;
            }

            _lastResult = result;
            _trials.Clear();
            foreach (ReactionTrialResult trial in result.Trials)
            {
                _trials.Add(trial);
            }
            ExportCsvButton.IsEnabled = _trials.Count > 0;

            string averageText = result.AverageReactionMs.HasValue
                ? result.AverageReactionMs.Value.ToString("F3", CultureInfo.InvariantCulture)
                : "N/A";
            SummaryTextBlock.Text =
                $"Trials: {result.TrialCount}, Valid: {result.ValidCount}, False Starts: {result.FalseStartCount}, Average: {averageText} ms";
            StatusTextBlock.Text = "Run completed.";
        }
        catch (Exception ex)
        {
            StatusTextBlock.Text = $"Run failed: {ex.Message}";
        }
        finally
        {
            StartRunButton.IsEnabled = true;
            if (_lastResult is not null && _lastResult.Trials.Count > 0)
            {
                ExportCsvButton.IsEnabled = true;
            }

            if (File.Exists(jsonPath))
            {
                try
                {
                    File.Delete(jsonPath);
                }
                catch
                {
                    // Non-fatal cleanup failure.
                }
            }
        }
    }
}
