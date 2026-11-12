# ATT_APP - GNSS signal-health monitor for NASA cFS
# Licensed under the Apache License, Version 2.0
#
# Write protocol: fill in the cFS command checksum. cFE defines it so that the
# XOR of every byte in the packet, checksum included, is 0xFF. It must run after
# the length protocol, because the length is part of what is summed.
require 'openc3/interfaces/protocols/protocol'

module OpenC3
  class AttAppCcsdsChecksum < Protocol
    def write_packet(packet)
      packet.write('CCSDS_CHECKSUM', 0)
      checksum = 0xFF
      packet.buffer.each_byte { |byte| checksum ^= byte }
      packet.write('CCSDS_CHECKSUM', checksum)
      packet
    end
  end
end
