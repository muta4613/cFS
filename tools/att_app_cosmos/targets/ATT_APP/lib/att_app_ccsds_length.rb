# ATT_APP - GNSS signal-health monitor for NASA cFS
# Licensed under the Apache License, Version 2.0
#
# Write protocol: fill in the CCSDS length field (total packet bytes - 7)
# so every command definition can stay correct if its payload changes.
require 'openc3/interfaces/protocols/protocol'

module OpenC3
  class AttAppCcsdsLength < Protocol
    CCSDS_PRIMARY_HEADER_BYTES = 6

    def write_packet(packet)
      packet.write('CCSDS_LENGTH', packet.length - CCSDS_PRIMARY_HEADER_BYTES - 1)
      packet
    end
  end
end
