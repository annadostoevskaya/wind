function decodeUplink(input) {
  var text = "";

  for (var i = 0; i < input.bytes.length; i++) {
    text += String.fromCharCode(input.bytes[i]);
  }

  var data = {};

  if (input.fPort === 2) {
    if (input.bytes.length === 15 && input.bytes[0] === 1) {
      data.voltage_a = readFixed2U16(input.bytes, 1);
      data.voltage_b = readFixed2U16(input.bytes, 3);
      data.voltage_c = readFixed2U16(input.bytes, 5);
      data.current_a = readFixed2U16(input.bytes, 7);
      data.current_b = readFixed2U16(input.bytes, 9);
      data.current_c = readFixed2U16(input.bytes, 11);
      data.revolutions = readFixed2U16(input.bytes, 13);
    } else {
      var values = parseFields(text);

      data.voltage_a = toNumber(values.VA);
      data.voltage_b = toNumber(values.VB);
      data.voltage_c = toNumber(values.VC);
      data.current_a = toNumber(values.IA);
      data.current_b = toNumber(values.IB);
      data.current_c = toNumber(values.IC);
      data.revolutions = toNumber(values.REV);
    }
  } else if (input.fPort === 3) {
    var values = parseFields(text);

    data.accel_x_raw = toNumber(values.X);
    data.accel_y_raw = toNumber(values.Y);
    data.accel_z_raw = toNumber(values.Z);
    data.battery_voltage = toNumber(values.BAT);

    var temperatures = (values.T || "").split(",");

    data.temperature_1 = parseTemperature(temperatures[0]);
    data.temperature_2 = parseTemperature(temperatures[1]);
    data.temperature_3 = parseTemperature(temperatures[2]);
    data.temperature_4 = parseTemperature(temperatures[3]);
  } else {
    return {
      data: {
        raw_payload: text,
        fPort: input.fPort
      },
      warnings: ["Unknown LoRaWAN port: " + input.fPort]
    };
  }

  return {
    data: data
  };
}

function readFixed2U16(bytes, offset) {
  return (bytes[offset] | (bytes[offset + 1] << 8)) / 100;
}

function parseFields(text) {
  var result = {};
  var fields = text.split(";");

  for (var i = 0; i < fields.length; i++) {
    var separator = fields[i].indexOf("=");

    if (separator < 0) {
      continue;
    }

    var key = fields[i].substring(0, separator);
    var value = fields[i].substring(separator + 1);

    result[key] = value;
  }

  return result;
}

function toNumber(value) {
  if (value === undefined || value === "") {
    return null;
  }

  var number = Number(value);
  return isNaN(number) ? null : number;
}

function parseTemperature(value) {
  if (value === undefined || value === "" || value === "ERR") {
    return null;
  }

  return toNumber(value);
}
