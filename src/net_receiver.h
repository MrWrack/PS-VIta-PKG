#pragma once
int net_receiver_init(char *ip_out, int ip_out_size);
void net_receiver_term(void);
int net_receive_one_vpk(const char *dest_dir, int port, char *saved_path, int saved_path_size, char *status, int status_size);

int net_receive_theme_zip(const char *dest_path, int port, char *status, int status_size);
