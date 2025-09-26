require 'mkmf'

# PCRE2 requires this to be defined before including the header
$CFLAGS << " -DPCRE2_CODE_UNIT_WIDTH=8"

# Try to use pkg-config if available
begin
  pcre2_cflags = `pkg-config --cflags libpcre2-8 2>/dev/null`.strip
  pcre2_libs = `pkg-config --libs libpcre2-8 2>/dev/null`.strip

  if $?.success? && !pcre2_libs.empty?
    $CFLAGS << " " << pcre2_cflags unless pcre2_cflags.empty?
    $LIBS << " " << pcre2_libs
  else
    # Fall back to manual linking if pkg-config not available
    $LIBS << " -lpcre2-8"
  end
rescue
  # Fall back if pkg-config command fails
  $LIBS << " -lpcre2-8"
end

unless find_header('pcre2.h')
  abort "-----\nPCRE2 is missing. You must install it as per the README @ https://github.com/adhearsion/ruby_speech\n-----"
end

create_makefile 'ruby_speech/ruby_speech'
