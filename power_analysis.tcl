# power_analysis.tcl
# Run in Vivado TCL console after implementation to generate power reports
# Usage: source power_analysis.tcl

puts "=== Zynq Low Power Sensor Interface — Power Analysis ==="
puts "Opening implemented design..."

open_run impl_1

# Basic power report to console
puts "\n--- Summary Power Report ---"
report_power

# Detailed report to file
set report_dir "C:/Users/DELL/OneDrive/Desktop/FinalYearProject"

report_power \
    -file "${report_dir}/power_report_summary.rpt"
puts "Summary report saved to: ${report_dir}/power_report_summary.rpt"

# Hierarchical report
report_power \
    -file "${report_dir}/power_report_hierarchical.rpt" \
    -hierarchical
puts "Hierarchical report saved to: ${report_dir}/power_report_hierarchical.rpt"

# Set switching activity for more accurate estimate
puts "\n--- Setting switching activity for accurate estimate ---"
set_switching_activity -default_toggle_rate 0.125
set_switching_activity -default_static_probability 0.5

report_power \
    -file "${report_dir}/power_report_with_activity.rpt"
puts "Activity-based report saved to: ${report_dir}/power_report_with_activity.rpt"

# Open interactive GUI report
report_power -name power_1
puts "\nInteractive power report opened in Vivado GUI"
puts "Check: Summary -> On-Chip Power for PL fabric estimate"

puts "\n=== Power Analysis Complete ==="
