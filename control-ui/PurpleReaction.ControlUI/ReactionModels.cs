using System.Text.Json.Serialization;

namespace PurpleReaction.ControlUI;

public sealed class ReactionRunResult
{
    [JsonPropertyName("trial_count")]
    public int TrialCount { get; set; }

    [JsonPropertyName("valid_count")]
    public int ValidCount { get; set; }

    [JsonPropertyName("false_start_count")]
    public int FalseStartCount { get; set; }

    [JsonPropertyName("average_reaction_ms")]
    public double? AverageReactionMs { get; set; }

    [JsonPropertyName("trials")]
    public List<ReactionTrialResult> Trials { get; set; } = [];
}

public sealed class ReactionTrialResult
{
    [JsonPropertyName("trial")]
    public int Trial { get; set; }

    [JsonPropertyName("random_delay_seconds")]
    public double RandomDelaySeconds { get; set; }

    [JsonPropertyName("reaction_ms")]
    public double? ReactionMs { get; set; }

    [JsonPropertyName("false_start")]
    public bool FalseStart { get; set; }

    public string RandomDelaySecondsDisplay => RandomDelaySeconds.ToString("F3");
    public string ReactionMsDisplay => ReactionMs.HasValue ? ReactionMs.Value.ToString("F3") : "-";
    public string FalseStartDisplay => FalseStart ? "Yes" : "No";
}
