#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${OUT_DIR:-$PROJECT_DIR/Captures}"
SNAPSHOT="${SNAPSHOT:-/tmp/FireWireOHCIProbe-ioreg-capture.txt}"
STAMP="$(date +%Y%m%d-%H%M%S)"

mkdir -p "$OUT_DIR"
ioreg -r -n FireWireOHCIProbe -l -w0 > "$SNAPSHOT"

perl -0777 - "$SNAPSHOT" "$OUT_DIR" "$STAMP" <<'PERL'
use strict;
use warnings;

my ($snapshot, $out_dir, $stamp) = @ARGV;
open my $fh, "<", $snapshot or die "$snapshot: $!";
my $txt = do { local $/; <$fh> };

sub prop_num {
    my ($name) = @_;
    return $1 + 0 if $txt =~ /"\Q$name\E" = (\d+)/;
    die "missing $name\n";
}

sub prop_str {
    my ($name) = @_;
    return $1 if $txt =~ /"\Q$name\E" = "([^"]+)"/;
    return "";
}

my $frames = prop_num("ProbeDigiDuplexIRCapturePCMFrameCount");
my $channels = prop_num("ProbeDigiDuplexIRCapturePCMChannelCount");
my $rate = prop_num("ProbeDigiDuplexIRCapturePCMSampleRate");
my $bytes_expected = prop_num("ProbeDigiDuplexIRCapturePCMBytes");
my $peak = prop_num("ProbeDigiDuplexIRCapturePCMPeakAbs");
my $success = prop_num("ProbeDigiDuplexSuccess");
my $cdhash = prop_str("IOUserServerCDHash");

$txt =~ /"ProbeDigiDuplexIRCapturePCMS24LE" = <([0-9a-fA-F\s]+)>/s
    or die "missing ProbeDigiDuplexIRCapturePCMS24LE\n";
(my $hex = $1) =~ s/\s+//g;
my $blob = pack("H*", $hex);
die "byte count mismatch got ".length($blob)." expected $bytes_expected\n"
    unless length($blob) == $bytes_expected;

my @s32 = unpack("l<*", $blob);
die "sample count mismatch\n" unless @s32 == $frames * $channels;

sub wav_header {
    my ($data_len, $ch, $sr, $bits) = @_;
    my $block = int($ch * $bits / 8);
    my $byte_rate = $sr * $block;
    return "RIFF" . pack("V", 36 + $data_len) . "WAVE" .
           "fmt " . pack("VvvVVvv", 16, 1, $ch, $sr, $byte_rate, $block, $bits) .
           "data" . pack("V", $data_len);
}

my $base = "$out_dir/digi003-capture-$stamp";
my $wav24 = "$base-8ch-24bit.wav";
open my $w24, ">:raw", $wav24 or die "$wav24: $!";
print $w24 wav_header($frames * $channels * 3, $channels, $rate, 24);
for my $v (@s32) {
    my $u = $v & 0x00ffffff;
    print $w24 pack("C3", $u & 0xff, ($u >> 8) & 0xff, ($u >> 16) & 0xff);
}
close $w24;

my $wav16 = "$base-ch1-2-normalized16.wav";
open my $w16, ">:raw", $wav16 or die "$wav16: $!";
print $w16 wav_header($frames * 2 * 2, 2, $rate, 16);
my $scale = $peak > 0 ? 30000 / $peak : 1;
for my $f (0 .. $frames - 1) {
    for my $ch (0, 1) {
        my $v = int($s32[$f * $channels + $ch] * $scale);
        $v = 32767 if $v > 32767;
        $v = -32768 if $v < -32768;
        print $w16 pack("s<", $v);
    }
}
close $w16;

my $raw = "$base-s32le.raw";
open my $rw, ">:raw", $raw or die "$raw: $!";
print $rw $blob;
close $rw;

my $meta = "$base-meta.txt";
open my $mw, ">", $meta or die "$meta: $!";
print $mw "success=$success\n";
print $mw "cdhash=$cdhash\n";
print $mw "frames=$frames\n";
print $mw "channels=$channels\n";
print $mw "sample_rate=$rate\n";
print $mw "bytes=$bytes_expected\n";
print $mw "peak_abs=$peak\n";
print $mw "snapshot=$snapshot\n";
close $mw;

print "success=$success frames=$frames channels=$channels rate=$rate bytes=$bytes_expected peak=$peak\n";
print "$wav24\n$wav16\n$raw\n$meta\n";
PERL
