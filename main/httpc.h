#ifndef _HTTPC_H
#define _HTTPC_H

esp_err_t httpc(QueueHandle_t stream);
void http_invalidate_last_modified(void);

#endif
