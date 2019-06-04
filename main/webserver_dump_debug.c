int task_cmp(const void *a, const void *b) {
    const TaskStatus_t *ta = (const TaskStatus_t *) a;
    const TaskStatus_t *tb = (const TaskStatus_t *) b;
    BaseType_t cmp = ta->xCoreID - tb->xCoreID;
    if (cmp != 0) return cmp;
    cmp = ta->eCurrentState - tb->eCurrentState;
    if (cmp != 0) return cmp;
    cmp = tb->uxCurrentPriority - ta->uxCurrentPriority;
    if (cmp != 0) return cmp;
    cmp = tb->uxBasePriority - ta->uxBasePriority;
    if (cmp != 0) return cmp;
    return strcmp(ta->pcTaskName, tb->pcTaskName);
}

void dump_tasks(UBaseType_t numberOfTasks, TaskStatus_t *tasks, uint32_t totalRunTime) {
    char *state;
    qsort(tasks, numberOfTasks, sizeof(TaskStatus_t), &task_cmp);
    strcat(http_resp,
           "<h1>RTOS Tasks States</h1>\n"
           "<table id='t1'><tr>"
           "<th>Name</th>"
           "<th>State</th>"
           "<th>CoreID</th>"
           "<th>Runtime [abs]</th>"
           "<th>Runtime [%]</th>"
           "<th>Priority</th>"
           "<th>Stack High Water Mark</th>"
           "</tr>\n"
    );

    uint32_t runTimeRel = totalRunTime / 100;
    if (totalRunTime < 1) {
        runTimeRel = 1;
    }
    for (int x = 0; x < numberOfTasks; x++) {
        switch (tasks[x].eCurrentState) {
            case eRunning:
                state = "Running";
                break;
            case eReady:
                state = "Ready";
                break;
            case eBlocked:
                state = "Blocked";
                break;
            case eSuspended:
                state = "Suspended";
                break;
            case eDeleted:
                state = "Deleted";
                break;
            default:
                state = "Unknown";
                break;
        }
        sprintf(http_resp + strlen(http_resp),
                "<tr>"
                "<td>%s</td>"
                "<td>%s</td>"
                "<td>%d</td>"
                "<td>%d</td>"
                "<td>%d%%</td>"
                "<td>current %d / base %d</td>"
                "<td>%d</td>"
                "</tr>\n",
                tasks[x].pcTaskName,
                state,
                tasks[x].xCoreID,
                tasks[x].ulRunTimeCounter,
                tasks[x].ulRunTimeCounter / runTimeRel,
                tasks[x].uxCurrentPriority,
                tasks[x].uxBasePriority,
                tasks[x].usStackHighWaterMark
        );
    }
    strcat(http_resp, "</table>\n");
}

void dump_tfa_tasks(const TFATaskManagerState *tfaState, UBaseType_t numberOfTasks, const TaskStatus_t *tasks) {
    struct tm timeinfo;
    strcat(http_resp,
           "<h1>TFA Tasks States</h1>\n"
           "<table id='t2'><tr>"
           "<th>Name</th>"
           "<th>TFA Reading Queue Stats</th>"
           "<th>Task Stack Memory Stats</th>"
           "</tr>\n");
    for (int x = 0; x < tfaState->runningTaskCount; x++) {
        uint32_t highWaterMark = -1;
        for (int i = 0; i < numberOfTasks; i++) {
            if (tasks[i].xHandle == tfaState->runningTasks[x].task) {
                highWaterMark = tasks[i].usStackHighWaterMark;
                break;
            }
        }
        sprintf(http_resp + strlen(http_resp),
                "<tr>"
                "<td>%s</td>"
                "<td>%d waiting + %d available = %d queue slots</td>"
                "<td>high water mark %d of total max %d bytes</td>"
                "<td><table class='t3'>\n",
                tfaState->runningTasks[x].name,
                uxQueueMessagesWaiting(tfaState->runningTasks[x].queue),
                uxQueueSpacesAvailable(tfaState->runningTasks[x].queue),
                tfaState->runningTasks[x].queueSize,
                highWaterMark,
                tfaState->runningTasks[x].stackSize
        );
        size_t offset = tfaState->runningTasks[x].logOffset;
        for (int i = (offset + 2) % TFA_TASK_LOG_SIZE; i != offset; i = (i + 1) % TFA_TASK_LOG_SIZE) {
            localtime_r(&tfaState->runningTasks[x].lastStartTimes[i], &timeinfo);
            strftime(http_resp + strlen(http_resp), sizeof(http_resp),
                     "<tr><td>%Y-%m-%d %H:%M:%S</td>",
                     &timeinfo);
            sprintf(http_resp + strlen(http_resp),
                    "<td>%lld &mu;s</td>"
                    "<td>%s 0x%x(%d)</td>"
                    "</tr>\n",
                    tfaState->runningTasks[x].lastDurations[i],
                    esp_err_to_name(tfaState->runningTasks[x].lastResults[i]),
                    tfaState->runningTasks[x].lastResults[i],
                    tfaState->runningTasks[x].lastResults[i]
            );
        }
        strcat(http_resp, "</table></td></tr>\n");
    }
    strcat(http_resp, "</table>\n");
}

void dump_various(const TFATaskManagerState *tfaState, UBaseType_t numberOfTasks, uint32_t totalRunTime) {
    UBaseType_t manchesterFree, manchesterRead, manchesterWrite, manchesterItemsWaiting;
    vRingbufferGetInfo(tfaState->manchesterState->buffer,
                       &manchesterFree, &manchesterRead, &manchesterWrite, &manchesterItemsWaiting);

    InfluxSenderState *influxState = tfaState->runningTasks[0].userData;

    struct tm timeinfo;
    localtime_r(&start_timestamp, &timeinfo);

    char *reset_reason;
    switch (esp_reset_reason()) {
        case ESP_RST_UNKNOWN:
        default:
            reset_reason = "UNKNOWN: Reset reason can not be determined";
            break;
        case ESP_RST_POWERON:
            reset_reason = "POWERON: Reset due to power-on event";
            break;
        case ESP_RST_EXT:
            reset_reason = "EXT: Reset by external pin (not applicable for ESP32)";
            break;
        case ESP_RST_SW:
            reset_reason = "SW: Software reset via esp_restart";
            break;
        case ESP_RST_PANIC:
            reset_reason = "PANIC: Software reset due to exception/panic";
            break;
        case ESP_RST_INT_WDT:
            reset_reason = "INT_WDT: Reset (software or hardware) due to interrupt watchdog";
            break;
        case ESP_RST_TASK_WDT:
            reset_reason = "TASK_WDT: Reset due to task watchdog";
            break;
        case ESP_RST_WDT:
            reset_reason = "WDT: Reset due to other watchdogs";
            break;
        case ESP_RST_DEEPSLEEP:
            reset_reason = "DEEPSLEEP: Reset after exiting deep sleep mode";
            break;
        case ESP_RST_BROWNOUT:
            reset_reason = "BROWNOUT: Brownout reset (software or hardware)";
            break;
        case ESP_RST_SDIO:
            reset_reason = "SDIO: Reset over SDIO";
            break;
    }


    sprintf(http_resp + strlen(http_resp),
            "<h1>Various</h1>\n"
            "<dl>"
            "<dt>number of tasks</dt><dd>%d</dd>\n"
            "<dt>total RTOS runtime</dt><dd>%d</dd>\n"
            "<dt>esp microseconds since boot</dt><dd>%lld</dd>\n"
            "<dt>log timestamp</dt><dd>%d</dd>\n"
            "<dt>last reset reason</dt><dd>%s</dd>\n"
            "<dt>free heap size</dt><dd>current %d / lowest %d</dd>\n"

            "<dt>manchester buffer indices</dt><dd>free %d / read %d / write %d / size %d</dd>\n"
            "<dt>manchester pending bytes</dt><dd>%d</dd>\n"

            "<dt>Influx send buffer</dt><dd>%d / %d bytes</dd>\n"
            "<dt>Influx offline buffer</dt><dd>%ld bytes</dd>\n",

            numberOfTasks,
            totalRunTime,
            esp_timer_get_time(),
            esp_log_timestamp(),
            reset_reason,
            esp_get_free_heap_size(),
            esp_get_minimum_free_heap_size(),

            manchesterFree,
            manchesterRead,
            manchesterWrite,
            tfaState->manchesterState->config.buffer_size,
            manchesterItemsWaiting,

            influxState->post_data_len,
            influxState->post_data_size,
            get_influx_offline_buffer_length()
    );
    strftime(http_resp + strlen(http_resp), sizeof(http_resp),
             "<dt>HTTP server running since</dt><dd>%Y-%m-%d %H:%M:%S</dd>"
             "</dl>\n",
             &timeinfo);
}
