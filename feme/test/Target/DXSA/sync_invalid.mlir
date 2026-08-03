// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'dxsa.sync' op uav_global and uav_group are mutually exclusive}}
dxsa.sync <uav_global|uav_group>
