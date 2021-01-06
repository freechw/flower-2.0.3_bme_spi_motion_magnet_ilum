    occupancy_sensor_type: {
        cluster: 'msOccupancySensing',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.hasOwnProperty('occupancySensorType')) {
                return {occupancy_sensor_type: msg.data.occupancySensorType};
            }
        },
    },