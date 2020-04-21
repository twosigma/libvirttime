# Copyright 2019 Two Sigma Investments, LP.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import time
import struct

NSEC_IN_SEC = 1000000000
PID_MAX_MAX = 4194304
THREAD_AREA_SIZE = 16 # timestruct size
VIRT_TIME_CONF_PATH = '/tmp/virttime-conf.bin'

def get_monotonic_clock_ns():
    if 'VIRT_TIME_CONF' in os.environ:
        raise RuntimeError("Must not be under libvirttime influence")

    try:
        return time.clock_gettime_ns(time.CLOCK_MONOTONIC)
    except AttributeError:
        return int(time.clock_gettime(time.CLOCK_MONOTONIC) * NSEC_IN_SEC)

class VirtTimeConfig:
    def ns_to_timestruct(ns):
        return struct.pack("@qq", ns // NSEC_IN_SEC, ns % NSEC_IN_SEC)
    def timestruct_to_ns(ts):
        (sec, nsec) = struct.unpack("@qq", ts)
        return sec * NSEC_IN_SEC + nsec

    def save(offset_ns, per_thread_ns_list):
        with open(VIRT_TIME_CONF_PATH, 'wb') as f:
            f.write(VirtTimeConfig.ns_to_timestruct(offset_ns))
            base_file_offset = f.seek(0, os.SEEK_CUR)
            for tid,ns in per_thread_ns_list:
                if tid >= PID_MAX_MAX:
                    raise RuntimeError("tid larger than PID_MAX_MAX: {}".format(tid))
                f.seek(base_file_offset + tid*THREAD_AREA_SIZE, os.SEEK_SET)
                f.write(VirtTimeConfig.ns_to_timestruct(ns))
            f.seek(base_file_offset + PID_MAX_MAX*THREAD_AREA_SIZE, os.SEEK_SET)
            f.write(b'\x00')

    def restore():
        offset_ns = -1
        per_thread_ns_list = []

        with open(VIRT_TIME_CONF_PATH, 'rb') as f:
            data = f.read(THREAD_AREA_SIZE)
            offset_ns = VirtTimeConfig.timestruct_to_ns(data)
            base_file_offset = f.seek(0, os.SEEK_CUR)
            tid = 1 # pid=0 does not exist
            while True:
                # Note the os.SEEK_DATA, we'll be skipping pages that have no
                # tids.
                file_offset = f.seek(base_file_offset + tid*THREAD_AREA_SIZE, os.SEEK_DATA)
                tid = (file_offset - base_file_offset)//THREAD_AREA_SIZE
                if tid >= PID_MAX_MAX:
                    break

                desired_file_offset = base_file_offset + tid*THREAD_AREA_SIZE
                if file_offset != desired_file_offset:
                    f.seek(desired_file_offset, os.SEEK_SET)

                data = f.read(THREAD_AREA_SIZE)
                per_thread_ns_list.append((tid, VirtTimeConfig.timestruct_to_ns(data)))

                tid += 1

        return (offset_ns, per_thread_ns_list)

def adjust_virttime_offset(app_offset_ns, previous_offset_ns, previous_per_thread_ns_list):
    machine_clock_ns = get_monotonic_clock_ns()
    offset_ns = machine_clock_ns - app_offset_ns
    per_thread_ns_list = [(tid,ns-previous_offset_ns+offset_ns)
                                for (tid,ns) in previous_per_thread_ns_list]
    return (offset_ns, per_thread_ns_list)

def read_virttime_app_time_ns():
    offset_ns, per_thread_ns_list = VirtTimeConfig.restore()
    machine_clock_ns = get_monotonic_clock_ns()
    app_offset_ns = machine_clock_ns - offset_ns
    return app_offset_ns

# First time run on host A
# This initializes the configuration file
offset_ns, per_thread_ns_list = adjust_virttime_offset(0, 0, [])
VirtTimeConfig.save(offset_ns, per_thread_ns_list)

# After checkpointing the application on host A
# This reads the time duration in ns that the application has been running for
# This quantity must be saved for its use on host B
checkpoint_time_offset_ns = read_virttime_app_time_ns()

# Before resuming the application on host B
# This applies the offset to all the timestruct in the configuration file.
offset_ns, per_thread_ns_list = VirtTimeConfig.restore()
offset_ns, per_thread_ns_list = adjust_virttime_offset(checkpoint_time_offset_ns, offset_ns, per_thread_ns_list)
VirtTimeConfig.save(offset_ns, per_thread_ns_list)
