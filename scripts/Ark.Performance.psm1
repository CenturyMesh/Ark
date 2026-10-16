Set-StrictMode -Version Latest
function Compare-ArkPerformance($Current,$Baseline) {
    foreach($report in @($Current,$Baseline)) {
        if(($report -is [Collections.IDictionary] -and $report.Contains('valid') -and -not $report.valid) -or ($report -isnot [Collections.IDictionary] -and $report.PSObject.Properties['valid'] -and -not $report.valid)){throw 'Incomplete performance attempt cannot be compared.'}
    }
    foreach($field in @('schema','workload','samples','machine_signature')) { if($Current.$field -ne $Baseline.$field){throw "Incomparable performance receipts: $field differs."} }
    if($Current.results.Count -ne 3 -or $Baseline.results.Count -ne 3){throw 'Performance comparison requires three project sizes.'}
    @(for($index=0;$index -lt 3;$index++){
        $a=$Current.results[$index];$b=$Baseline.results[$index]
        if($a.feature_count -ne $b.feature_count -or $a.fixture_sha256 -ne $b.fixture_sha256){throw 'Incomparable fixture count/hash.'}
        foreach($metric in @('fresh_process_open','warm_open','incremental_save','interaction_doc_info','recompute_changed_parameter')){
            foreach($statistic in @('median_ms','p95_ms')){
                $before=[double]$b.$metric.$statistic;$after=[double]$a.$metric.$statistic
                if($before -le 0 -or $after -lt 0 -or [double]::IsNaN($before) -or [double]::IsNaN($after) -or [double]::IsInfinity($before) -or [double]::IsInfinity($after)){throw 'Invalid performance sample.'}
                @{feature_count=$a.feature_count;metric=$metric;statistic=$statistic;baseline_ms=$before;current_ms=$after;ratio=$after/$before}
            }
        }
    })
}
Export-ModuleMember -Function Compare-ArkPerformance
