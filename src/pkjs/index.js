var xhrRequest = function(url, type, callback) {
    var xhr = new XMLHttpRequest();
    xhr.onload = function() { callback(this.responseText); };
    xhr.open(type, url);
    xhr.send();
};

function weatherCodeToCondition(code) {
    if (code === 0) return 'Clear';
    if (code <= 3)  return 'Cloudy';
    if (code <= 48) return 'Fog';
    if (code <= 55) return 'Drizzle';
    if (code <= 57) return 'Drizzle';
    if (code <= 65) return 'Rain';
    if (code <= 67) return 'Rain';
    if (code <= 75) return 'Snow';
    if (code <= 77) return 'Snow Grains';
    if (code <= 82) return 'Showers';
    if (code <= 86) return 'Snow Shwrs';
    if (code === 95) return 'T-Storm';
    if (code <= 99)  return 'T-Storm';
    return 'Cloudy';
}

function locationSuccess(pos) {
    var url = 'https://api.open-meteo.com/v1/forecast?' +
        'latitude='  + pos.coords.latitude +
        '&longitude=' + pos.coords.longitude +
        '&current=temperature_2m,weather_code';

    xhrRequest(url, 'GET', function(responseText) {
        try {
            var json = JSON.parse(responseText);
            var temperature = Math.round(json.current.temperature_2m);
            var conditions  = weatherCodeToCondition(json.current.weather_code);
            Pebble.sendAppMessage(
                { 'TEMPERATURE': temperature, 'CONDITIONS': conditions },
                function() { console.log('Weather sent'); },
                function(e) { console.log('Weather send failed: ' + JSON.stringify(e)); }
            );
        } catch(e) {
            console.log('Weather parse error: ' + e);
        }
    });
}

function locationError(err) {
    console.log('Location error: ' + err.message);
}

function getWeather() {
    navigator.geolocation.getCurrentPosition(locationSuccess, locationError,
        { timeout: 15000, maximumAge: 60000 });
}

Pebble.addEventListener('ready', function() {
    console.log('PebbleKit JS ready');
    getWeather();
});

Pebble.addEventListener('appmessage', function(e) {
    if (e.payload['REQUEST_WEATHER']) {
        getWeather();
    }
});
