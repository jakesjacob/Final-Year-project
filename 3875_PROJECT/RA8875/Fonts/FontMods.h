// This file contains a perl script used to modify the mikroe generated fonts.
//
// Do not "include" this file into your project - just copy everything from
// the #if to the #endif and put it into a .pl file on your local PC to
// run it (this script requires that you have Perl installed).
#if 0

# ParseMikroeFont.pl
#
# Parse and Modify Mikroe Font File and make a few strategic changes
#
# Copyright (c) 2019 by Smartware Computing, all rights reserved.
#
use strict;
use warnings; 
use POSIX;

my @HelpInfo = (
    "                                                                           ",
    "This script modifies a font file which was generated with a tool by        ",
    "MikroElektronika - GLD Font Creator.                                       ",
    "                                                                           ",
    "That tool creates the font data set for an embedded system from a Windows  ",
    "True Type font. The user is encouraged to ensure that the font used is     ",
    "properly licensed, or drawn from a source that does not have a license     ",
    "restriction.                                                               ",
    "                                                                           ",
    "This script will read and then modify the file for a few specific purposes:",
    "  * <space>   character is redefined to set the width to 1/4 the height,   ",
    "              because the normal behavior sets it much too narrow.         ",
    "  * '0' - '9' characters are redefined to set the width equal to the width ",
    "              of the widest digit, or to the user override value.          ",
    "                                                                           ",
    "And just because it can, it then improves upon the documentation in the    ",
    "resulting data structure.                                                  ",
    "                                                                           ",
    "This script was created by Smartware Computing, and is provided 'as is'    ",
    "with no warranty or suitability of fitness for any purpose. Anyone may use ",
    "or modify it subject to the agreement that:                                ",
    "  * The Smartware copyright statement remains intact.                      ",
    "  * Modifications for derivative use are clearly stated in this header.    ",
    "                                                                           ",
    "Modifications from the original:                                           ",
    "  * none.                                                                  ",
    "                                                                           ",
);


my $FixFile = 1;        # Fix the file with planned changes
my $Details = 1;        # Emit more detailed information
my $Debug = 0;          # 0=None, 1=Some, 2=Detailed

my $DigitWidth = 0;     # can be set on the command line, or it uses width('0')
my $ff = "";            # FontFile
my $of = "";            # Output File - otherwise stdout
my $OH;                 # Handle to the output file

my $prg = $0;
$prg =~ s/.*[\\\/]//;
$prg =~ s/\.pl//i;
my $args = join(" ", @ARGV);

if (!@ARGV) {
    ShowHelp();
    exit;
}
foreach (@ARGV) {
    if (/-0=(\d+)/) {
        $DigitWidth = $1;
    } elsif (/-d=(\d+)/) {
        $Debug = $1;
    } elsif ($ff eq "") {
        $ff = $_;
    } elsif ($of eq "") {
        $of = $_;
    } else {
        ShowHelp();
        exit;
    }
}
if (! -e $ff) {
    printf("Can't read $ff\n");
    ShowHelp();
    exit;
}
if (! -T $ff) {
    printf("File $ff isn't text.\n");
    ShowHelp();
    exit;
}
if ($of ne "") {
    open($OH, ">$of") || die("Can't write to $of");
    select($OH);
}

my @data = ();      # Raw byte stream from the source file
my @FileTop;
my $FontDeclaration;
my @FileBot;
my %charData;       # charData{$char}{width}, {$char}{data}

ImportFontFile();
DumpRawData() if ($Debug >= 2);

my $unk_0 = GetValueAt_Size_(0,1);
my $unk_1 = GetValueAt_Size_(1,1);

my $firstChar = GetValueAt_Size_(2,2);
my $lastChar = GetValueAt_Size_(4,2);
my $fontHeight = GetValueAt_Size_(6,1);
my $unk_2 = GetValueAt_Size_(7,1);

printf("// Char Range [%4X - %4X]\n", $firstChar, $lastChar) if ($Debug);

for (my $char = $firstChar; $char <= $lastChar; $char++) {
    my $offsetToChar = 8 + 4 * ($char - $firstChar);
    my $charWidth = GetValueAt_Size_($offsetToChar,1);
    my $charDataNdx = GetValueAt_Size_($offsetToChar + 1, 2);
    $charData{$char}{width} = $charWidth;
    my $count = (floor(($charWidth+7)/8));
    @{$charData{$char}{data}} = GetByteArrayAt_Size_($charDataNdx, $count * $fontHeight);
}

ShowFonts() if ($Debug >= 2);   # Before Modifications
FixChars() if ($FixFile);
ShowFonts() if ($Debug);        # After Modifications
EmitFile();
if ($of ne "") {
    select(STDOUT);
    close($OH);
}
exit;

#########################################################################

sub FixChars {
    my @newDat;
    my $char;
    my $charWidth;
    my $BytesWide;

    #
    # * <space>   character is redefined to set the width to 1/4 the height.
    #
    $char = 0x20;                           # Fix <space>
    $charWidth = floor($fontHeight/4);
    $charData{$char}{width} = $charWidth;
    $BytesWide = floor($charWidth/8);
    for (my $i=0; $i<($BytesWide*$fontHeight); $i++) {
        $charData{$char}{data}[$i] = 0x00;
    }
    
    #
    # * '0' - '9' characters are redefined to set the width equal to width('0')
    #             or to the user override value.                               
    #
    if ($DigitWidth > 0) {
        $charWidth = $DigitWidth;   # User override option
    } else {
        $charWidth = DigitMaxWidth();           # Set it to the width of the widest digit
    }
    $BytesWide = floor(($charWidth+7)/8);
    #printf("Set Width = $charWidth, BytesWide = $BytesWide, Height = $fontHeight\n");
    for ($char = 0x30; $char <= 0x39; $char++) {
        for (my $h=0; $h<$fontHeight; $h++) {
            for (my $w=0; $w<$BytesWide; $w++) {
                my $pDst = $h * $BytesWide + $w;
                if ($w < ceil($charData{$char}{width}/8)) {
                    my $pSrc = $h * floor(($charData{$char}{width}+7)/8) + $w;
                    $newDat[$pDst] = $charData{$char}{data}[$pSrc];
                } else {
                    $newDat[$pDst] = 0x00;
                }
            }
        }
        $charData{$char}{width} = $charWidth;
        for (my $i=0; $i<($fontHeight * $BytesWide); $i++) {
            $charData{$char}{data}[$i] = $newDat[$i];
        }
        #RenderChar($char);
        #<stdin>;
    }
}

sub DigitMaxWidth {
    my $max = 0;
    for (my $char=0x30; $char <= 0x39; $char++) {
        $max  = $charData{$char}{width} if ($max < $charData{$char}{width});
    }
    return $max;
}

sub ImportFontFile {
    # 0 = scanning
    # 1 = after '{'
    # 2 = found '}'
    my $state = 0;

    open(FH, "<$ff") || die("Can't open $ff");
    while (<FH>) {
        my $rec = $_;
        chomp $rec;
        if ($state == 0) {
            if ($rec =~ /^const .*{/) {
                $FontDeclaration = $rec;
                $state = 1;
            } else {
                push @FileTop, $rec;
            }
        } elsif ($state == 1) {
            if ($rec =~ /};/) {
                $rec =~ s/^ +(.*)$/$1/ if ($Details);
                push @FileBot, $rec;
                $state = 2;
            } else {
                $rec =~ s/( +)/ /g;
                next if ($rec =~ /^ *$/);
                $rec =~ s# +//.*##;
                $rec =~ s/^ +(.*)$/$1/;
                $rec =~ s/^(.*),$/$1/;
                $rec =~ s/0x//g;
                push @data, split(",", $rec);
            }
        } elsif ($state == 2) {
            push @FileBot, $rec;
        }
    }
    close FH;
}


sub ShowHelp {
    print "\n\n$prg\n\n";
    foreach (@HelpInfo) {
        print "    $_\n";
    }
    print <<EOM;
$prg <MikroeFontFile> [Options] [<OptionalNewFile>]

    Process the MikreFontFile, optionally generating a new file.

    Options:
     -0=xx      Set Digit '0' - '9' width to xx
     -d=x       Set Debug Level 0=None, 1=Some, 2=More

EOM
}

sub ShowFonts {
    for (my $char = $firstChar; $char <= $lastChar; $char++) {
        my $charWidth = $charData{$char}{width};
        printf("\n// === %d (0x%2X) === w:%d, h:%d\n", $char, $char, $charWidth, $fontHeight);
        RenderChar($char);
    }
}

sub EmitFile {
    if ($Details) {
        foreach (@HelpInfo) {
            print "//    $_\n";
        }
        print "// Script Activation:\n";
        printf("//   %s %s\n", $prg, $args);
        print "\n";
    }

    print join("\n", @FileTop) . "\n";      # Mikroe header
    printf("%s\n", $FontDeclaration);
    printf("    // Font Info\n") if ($Details);
    printf("    0x%02X,                   // Unknown #1\n", $unk_0);
    printf("    0x%02X,                   // Unknown #2\n", $unk_1);
    printf("    %s,              // FirstChar\n", HexStream($firstChar,2));
    printf("    %s,              // LastChar\n", HexStream($lastChar,2));
    printf("    %s,                   // FontHeight\n", HexStream($fontHeight,1));
    printf("    0x%02X,                   // Unknown #3\n", $unk_2);
    
    printf("    // Directory of Chars  [Width] [Offset-L] [Offset-M] [Offset-H]\n") if ($Details);
    my $offsetToChar = 8 + 4 * ($lastChar - $firstChar + 1);
    for (my $char = $firstChar; $char <= $lastChar; $char++) {
        my $charWidth = $charData{$char}{width};
        my $charByteCount = floor(($charWidth+7)/8) * $fontHeight;
        my $asc = ($char >= 0x20 && $char < 0x7F) ? chr($char) : "<non-printable>";
        $asc = "\\\\" if ($char == 0x5C);
        my $details = ($Details) ? sprintf("    // 0x%02X '%s' ", $char, $asc) : "";
        printf("    0x%02X,%s,0x%02X,%s\n",
            $charWidth, HexStream($offsetToChar,2), 0,
            $details
        );
        $offsetToChar += $charByteCount;
    }
    printf("    // Chars Bitstream\n") if ($Details);
    for (my $char = $firstChar; $char <= $lastChar; $char++) {
        my $charWidth = $charData{$char}{width};
        my $charByteCount = floor(($charWidth+7)/8) * $fontHeight;
        my $asc = ($char >= 0x20 && $char < 0x7F) ? chr($char) : "<non-printable>";
        $asc = "\\\\" if ($char == 0x5C);
        my $string = GetSomeHexBytes($char, 0, 8 * $charByteCount);
        printf("    %s%s  // 0x%02X '%s'\n",
            $string, 
            ($char != $lastChar) ? "," : "",
            $char, $asc
        );
    }
    print join("\n", @FileBot);
}

sub DumpRawData {
    my $i;
    print "// ";
    for ($i=0; $i<=$#data; $i++) {
        printf("%02X ", hex($data[$i]));
        print "\n// " if ($i % 16 == 15);
    }
    print "\n";
}


sub HexStream {
    my ($value, $len) = @_;
    my @parts;
    while ($len--) {
        push @parts, sprintf("0x%02X", $value & 0xFF);
        $value >>= 8;
    }
    return join(",", @parts);
}

sub RenderChar {
    my ($char) = shift;
    my $h = $fontHeight;
    my $w = $charData{$char}{width};
    
    PrintChar($char,$w,$h,@{$charData{$char}{data}});
}

sub PrintChar {
    my ($char,$w,$h,@datablock) = @_;
    
    printf("//     +%s+    \n// ", '-' x $w);
    my $row = 0;
    my $boolStream = 0;
    while ($h--) {
        my $pixels = $w;
        my $bitmask = 0x01;
        my $rowStream = $boolStream;
        printf("%02X  |", $row++);
        my $tail = "";
        while ($pixels) {
            my $byte;
            $datablock[$rowStream] = 0 if (!defined($datablock[$rowStream]));
            $byte = $datablock[$rowStream];
            printf("%s", ($byte & $bitmask) ? "*" : " ");
            $bitmask <<= 1;
            if ($pixels > 1 && ($bitmask & 0xFF) == 0) {
                $bitmask = 0x01;
                $rowStream++;
            }
            $pixels--;
        }
        printf("|    %s\n// ", $tail);
        $boolStream += ($rowStream - $boolStream + 1);
    }
    printf("    +%s+\n", '-' x $w);
}


sub GetSomeHexBytes {
    my ($char, $offSet, $w) = @_;
    my @out;
    my $x = 0;
    
    $w = floor(($w+7)/8);
    while ($w--) {
        my $c = 0;
        $c = $charData{$char}{data}[$offSet + $x++] if (defined($charData{$char}{data}[$offSet + $x]));
        push @out, sprintf("0x%02X", $c);
    }
    return join(",", @out);
}

sub GetSomeBytes {
    my ($char, $offSet, $w) = @_;
    my $string = "";
    my $x = 0;
    
    $w = floor(($w+7)/8);
    while ($w--) {
        $string .= sprintf("%02X ", $charData{$char}{data}[$offSet + $x++]);
    }
    return $string;
}

sub GetValueAt_Size_ {
    my ($offset, $size) = @_;
    my $value = 0;
    while ($size--) {
        $value = ($value << 8) | hex($data[$offset + $size]);
    }
    return $value;
}

sub GetValueFromArray_At_Size_ {
    my ($offset, $size, $ary) = @_;
    my $value = 0;
    while ($size--) {
        $value = ($value << 8) | hex($$ary[$offset + $size]);
    }
    return $value;
}

sub GetByteArrayAt_Size_ {
    my ($offset, $size) = @_;
    my @bytes;
    while ($size--) {
        push @bytes, hex($data[$offset++]);
    }
    return @bytes;
}

sub GetByteSreamAt_Size_ {
    my ($offset, $size) = @_;
    my $value = "";
    while ($size--) {
        $value .= sprintf("%02X ", hex($data[$offset++]));
    }
    return $value;
}

#endif