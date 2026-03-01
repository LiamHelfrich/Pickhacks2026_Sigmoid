Microcontroller and server code for the Bird Search project (PickHacks 2026)

Branch main has code for the microcontroller, which uploads audio data to the VPS. Code is found under the 'ESP_Code folder'.
Branch rest_api has code for the VPS, which runs the [BirdNet Classifier](https://birdnet.cornell.edu/) on the audio and logs detected birds to a MongoDB instance.
