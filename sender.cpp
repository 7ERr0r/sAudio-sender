





#include <iostream>
#include <boost/array.hpp>
#include <boost/shared_array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include "portaudio.h"
#include "opus.h"

#define SAMPLE_RATE   (48000)
#define FRAMES_PER_BUFFER  (240)



#define BUFFER_SIZE   (16)

struct sAudioBuffer {
    int error;
    OpusEncoder *enc;
    int length[BUFFER_SIZE];
    unsigned char buffer[BUFFER_SIZE][4000];
    int writerIndex;
    int readerIndex;
};


static int audioCallback( const void *inputBuffer, void *outputBuffer,
                            unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void *userData ){

    sAudioBuffer *data = (sAudioBuffer*)userData;
    float *in = (float*)inputBuffer;

    (void) timeInfo;
    (void) statusFlags;
    (void) outputBuffer;

    data->length[data->writerIndex] = opus_encode_float(data->enc, in, framesPerBuffer, data->buffer[data->writerIndex], 4000);


    if(data->length[data->writerIndex] < 0){
        data->error = data->length[data->writerIndex];
        data->length[data->writerIndex] = 0;
    }


    if(++data->writerIndex >= BUFFER_SIZE){
        data->writerIndex = 0;
    }

    return paContinue;
}



static void StreamFinished( void* userData ){
    //sAudioBuffer *data = (sAudioBuffer*) userData;
}
boost::array<unsigned char, 1<<15> send_arr;
boost::array<unsigned char, 1<<15> recv_arr;
class sAudioReceiver {
private:
    sAudioBuffer* audiobuf;
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint receiver_endpoint;
    boost::asio::ip::udp::endpoint remote_endpoint;
    boost::posix_time::milliseconds* ping_interval;
    boost::asio::deadline_timer* ping_timer;
    int sequentialId;
public:
    bool synchronized = false;

    sAudioReceiver(boost::asio::io_service& io_service, sAudioBuffer* audiobuf, char* host, int port) : audiobuf(audiobuf), socket(io_service), sequentialId(0) {
        printf("Creating sAudioReceiver on %s:%d\n", host, port);
        socket.open(boost::asio::ip::udp::v4());

        receiver_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(host), port);
        int CHANNELS = 2;
        int FRAME_SIZE_MS = 5;
        int FRAME_SIZE = (CHANNELS * SAMPLE_RATE * FRAME_SIZE_MS) / 1000;

        std::string req("ihazi");
        std::copy(req.begin(), req.end(), send_arr.begin());
        printf("Created sAudioReceiver\n");
        send_request();
        printf("Sent request\n");
        ping_interval = new boost::posix_time::milliseconds(FRAME_SIZE_MS);
        ping_timer = new boost::asio::deadline_timer(socket.get_io_service(), *ping_interval);
        start_timer();
    }

    void start_timer(){
        ping_timer->expires_at(ping_timer->expires_at() + *ping_interval);
        ping_timer->async_wait(boost::bind(
            &sAudioReceiver::handle_timer,
            this,
            boost::asio::placeholders::error
        ));
    }
    void send_request(){
        socket.async_send_to(boost::asio::buffer(send_arr, 5), receiver_endpoint,
            boost::bind(&sAudioReceiver::handle_send, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred
            )
        );
    }
    void send_audio(){
        if(audiobuf->error != 0){
            std::cout << audiobuf->error << "\n";
            return;
        }
        if(audiobuf->readerIndex == audiobuf->writerIndex) audiobuf->readerIndex = (BUFFER_SIZE+audiobuf->writerIndex-1)%BUFFER_SIZE;
        memcpy(&send_arr[0], (unsigned char*) &sequentialId, 4);
        int len = audiobuf->length[audiobuf->readerIndex];
        memcpy(&send_arr[4], audiobuf->buffer[audiobuf->readerIndex], len);


        std::cout << "len: " << len << "\n";
        socket.async_send_to(boost::asio::buffer(send_arr, len+4), receiver_endpoint,
            boost::bind(&sAudioReceiver::handle_send, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred
            )
        );
        audiobuf->length[audiobuf->readerIndex] = 0;
        if(++audiobuf->readerIndex >= BUFFER_SIZE){
            audiobuf->readerIndex = 0;
        }
        sequentialId++;



    }
    void handle_timer(const boost::system::error_code& error){
        if (!error || error == boost::asio::error::message_size){
            send_audio();
            start_timer();
        }else{
            std::cout << "timer error: " << error.message() << "\n";
        }

    }


    void handle_send(const boost::system::error_code& error, std::size_t){
        if (!error || error == boost::asio::error::message_size){

        }
    }


};


void serveClient(char* host, sAudioBuffer* audiobuf){
    printf("Starting listener\n");
    boost::asio::io_service io_service;
    sAudioReceiver receiver(io_service, audiobuf, host, 42381);

    io_service.run();
}


int main(int argc, char* argv[]){
    char* host = "127.0.0.1";
    if (argc != 2){
        std::cerr << "Usage: sender <host>" << std::endl;
        return 1;
    }else{
        host = argv[1];
    }
    PaStreamParameters inputParameters;
    PaStream *stream;
    PaError err;
    sAudioBuffer* audiobuf;
    audiobuf = new sAudioBuffer();
    audiobuf->enc = opus_encoder_create(SAMPLE_RATE, 2, OPUS_APPLICATION_AUDIO, &audiobuf->error);
    if(audiobuf->error != OPUS_OK){
        std::cerr << "opus: could not create decoder" << std::endl;
        return 2;
    }

    printf("SR = %d, BufSize = %d\n", SAMPLE_RATE, FRAMES_PER_BUFFER);


    audiobuf->writerIndex = audiobuf->readerIndex = 0;


    err = Pa_Initialize();
    if( err != paNoError ) goto error;

    inputParameters.device = Pa_GetDefaultInputDevice();
    if (inputParameters.device == paNoDevice){
        fprintf(stderr,"Error: No default input device.\n");
        goto error;
    }
    printf( "Input device: %d.\n", inputParameters.device );
    printf( "Input LL: %g s\n", Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency );
    printf( "Input HL: %g s\n", Pa_GetDeviceInfo( inputParameters.device )->defaultHighInputLatency );
    inputParameters.channelCount = 2;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
            &stream,
            &inputParameters,
            NULL,
            SAMPLE_RATE,
            FRAMES_PER_BUFFER,
            paClipOff,
            audioCallback,
            audiobuf);

    if( err != paNoError ) goto error;


    err = Pa_SetStreamFinishedCallback( stream, &StreamFinished );
    if( err != paNoError ) goto error;

    err = Pa_StartStream( stream );
    if( err != paNoError ) goto error;



    try{
        serveClient(host, audiobuf);
    }catch (std::exception& e){
        std::cerr << e.what() << std::endl;
    }


    err = Pa_StopStream( stream );
    if( err != paNoError ) goto error;

    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto error;

    Pa_Terminate();

    delete audiobuf;
    return 0;
error:
    Pa_Terminate();
    fprintf( stderr, "An error occured while using the portaudio stream\n" );
    fprintf( stderr, "Error number: %d\n", err );
    fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
    return err;
}

