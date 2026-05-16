/******/ (function(modules) { // webpackBootstrap
/******/ 	// The module cache
/******/ 	var installedModules = {};
/******/
/******/ 	// The require function
/******/ 	function __webpack_require__(moduleId) {
/******/
/******/ 		// Check if module is in cache
/******/ 		if(installedModules[moduleId])
/******/ 			return installedModules[moduleId].exports;
/******/
/******/ 		// Create a new module (and put it into the cache)
/******/ 		var module = installedModules[moduleId] = {
/******/ 			exports: {},
/******/ 			id: moduleId,
/******/ 			loaded: false
/******/ 		};
/******/
/******/ 		// Execute the module function
/******/ 		modules[moduleId].call(module.exports, module, module.exports, __webpack_require__);
/******/
/******/ 		// Flag the module as loaded
/******/ 		module.loaded = true;
/******/
/******/ 		// Return the exports of the module
/******/ 		return module.exports;
/******/ 	}
/******/
/******/
/******/ 	// expose the modules object (__webpack_modules__)
/******/ 	__webpack_require__.m = modules;
/******/
/******/ 	// expose the module cache
/******/ 	__webpack_require__.c = installedModules;
/******/
/******/ 	// __webpack_public_path__
/******/ 	__webpack_require__.p = "";
/******/
/******/ 	// Load entry module and return exports
/******/ 	return __webpack_require__(0);
/******/ })
/************************************************************************/
/******/ ([
/* 0 */
/***/ (function(module, exports, __webpack_require__) {

	__webpack_require__(1);
	module.exports = __webpack_require__(2);


/***/ }),
/* 1 */
/***/ (function(module, exports) {

	/**
	 * Copyright 2024 Google LLC
	 *
	 * Licensed under the Apache License, Version 2.0 (the "License");
	 * you may not use this file except in compliance with the License.
	 * You may obtain a copy of the License at
	 *
	 *     http://www.apache.org/licenses/LICENSE-2.0
	 *
	 * Unless required by applicable law or agreed to in writing, software
	 * distributed under the License is distributed on an "AS IS" BASIS,
	 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	 * See the License for the specific language governing permissions and
	 * limitations under the License.
	 */
	
	(function(p) {
	  if (!p === undefined) {
	    console.error('Pebble object not found!?');
	    return;
	  }
	
	  // Aliases:
	  p.on = p.addEventListener;
	  p.off = p.removeEventListener;
	
	  // For Android (WebView-based) pkjs, print stacktrace for uncaught errors:
	  if (typeof window !== 'undefined' && window.addEventListener) {
	    window.addEventListener('error', function(event) {
	      if (event.error && event.error.stack) {
	        console.error('' + event.error + '\n' + event.error.stack);
	      }
	    });
	  }
	
	})(Pebble);


/***/ }),
/* 2 */
/***/ (function(module, exports) {

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


/***/ })
/******/ ]);
//# sourceMappingURL=pebble-js-app.js.map