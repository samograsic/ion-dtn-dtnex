# Configurable parameters
ION_CONFIG_FILE="/home/samo/dtn/host268435600.rc"
ION_LOG_PATH="/home/samo/dtn/ion.log"  # Configurable ion.log path
SDRWATCH_LOG_PATH="/home/samo/dtn/sdrwatch.log"  # Configurable sdrwatch.log path
BPLIST_LOG_PATH="/home/samo/dtn/bplist.log"  # Configurable bplist.log path
START_SLEEP=5
WATCH_INTERVAL=15
TIMEOUT=2
LOG_PATH="/home/samo/dtn"  # Use the script's directory as default

# Function to start the Ion service
start_ion_service() {
    ionstart -I "$ION_CONFIG_FILE"
}

# Function to stop the Ion service
stop_ion_service() {
    ionstop
}

# Function to log ion.log file, conditions, sdrwatch output, and bplist detail
log_ion_conditions() {
    local condition_type="$1"
    local date_time="$(date +'%Y%m%d_%H%M%S')"
    local folder_name="$LOG_PATH/$date_time"
    local ion_log_file_name="$date_time_$condition_type.log"
    local sdrwatch_log_file_name="sdrwatch.log"
    local bplist_log_file_name="bplist.log"

    mkdir -p "$folder_name"
    cp "$ION_CONFIG_FILE" "$folder_name/"

    # Log ion.log file
    mv "$ION_LOG_PATH" "$folder_name/$ion_log_file_name"
    echo "$date_time - Created snapshot folder: $folder_name"

    # Log sdrwatch output
    sdrwatch ion > "$folder_name/$sdrwatch_log_file_name" 2>&1
    echo "$date_time - Logged sdrwatch output to: $folder_name/$sdrwatch_log_file_name"

    # Log bplist detail
    bplist detail > "$folder_name/$bplist_log_file_name" 2>&1
    echo "$date_time - Logged bplist detail to: $folder_name/$bplist_log_file_name"
}

while true; do
    # Run bpadmin command to check Ion service and targeted service condition
    response=$(timeout "$TIMEOUT" bpadmin <<< "l" 2>&1 | tr -d '\r\n')
    exit_code="$?"

    if [ -n "$response" ]; then
        echo "$datetime - bpadmin output: $response"
    fi
    datetime="$(date +'%Y-%m-%d %H:%M:%S')"

    if [ "$exit_code" -eq 0 ] && [[ "$response" == *List\ what?:* ]]; then
        echo "$datetime - Ion service is running. Targeted service condition is met."
        echo "q" | bpadmin
    else
        if [ "$exit_code" -eq 124 ]; then
            # Log conditions
            log_ion_conditions "ionsdrlocked"  # Log timeout condition

            # Timeout occurred, use "ionunlock" command
            ionunlock
            sleep "$START_SLEEP"
        else
            # Log conditions
            log_ion_conditions "ionnotrunning"  # Log ionnotrunning condition
            start_ion_service

            # Wait for a short time to allow the Ion service to start
            sleep "$START_SLEEP"

            # Check if the Ion service started successfully
            response=$(timeout "$TIMEOUT" bpadmin <<< "l" 2>&1 | tr -d '\r\n')
            exit_code="$?"

            if [ "$exit_code" -eq 0 ] && [[ "$response" == *List\ what?:* ]]; then
                echo "$datetime - Ion service started successfully."
            else
                echo "$datetime - Failed to start Ion service."
            fi
        fi
    fi

    sleep "$WATCH_INTERVAL"
done