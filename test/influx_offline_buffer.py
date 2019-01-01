import ctypes
from os import system

system("gcc -shared influx_offline_buffer.c -o influx_offline_buffer.so -fPIC")

libc = ctypes.CDLL("libc.so.6")
influx_offline_buffer = ctypes.CDLL("./influx_offline_buffer.so")

send_influx_write_t = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t)
actual_data = bytearray()
expected_data = bytearray()
write_count = 0


@send_influx_write_t
def send_influx_write(client, post_data, len):
    global write_count
    print("send_influx_write ", post_data[0:len], len)
    actual_data.extend(bytearray(post_data)[0:len])
    if write_count == 0:
        appended = ("0123456789" * 10 + "\n") * 5
        libc.sprintf(post_data, appended)
        expected_data.extend(appended)
        influx_offline_buffer.store_influx_offline_buffer(post_data, libc.strlen(post_data))
    write_count += 1
    return True


influx_offline_buffer.set_send_influx_write(send_influx_write)

POST_DATA_SIZE = ctypes.c_size_t.in_dll(influx_offline_buffer, "POST_DATA_SIZE").value
post_data = ctypes.create_string_buffer(("0123456789" * 27 + "\n") * 3, POST_DATA_SIZE)
influx_offline_buffer.store_influx_offline_buffer(post_data, libc.strlen(post_data))
influx_offline_buffer.store_influx_offline_buffer(post_data, libc.strlen(post_data))
influx_offline_buffer.store_influx_offline_buffer(post_data, libc.strlen(post_data))
influx_offline_buffer.store_influx_offline_buffer(post_data, libc.strlen(post_data))
libc.sprintf(post_data, ("0123456789" * 27 + "\n"))
influx_offline_buffer.store_influx_offline_buffer(post_data, libc.strlen(post_data))

expected_data.extend(open(ctypes.c_char_p.in_dll(influx_offline_buffer, "INFLUX_BUFFER_FILE").value, "rb").read())

influx_offline_buffer.send_influx_offline_buffer(None, post_data)

print(actual_data == expected_data)
