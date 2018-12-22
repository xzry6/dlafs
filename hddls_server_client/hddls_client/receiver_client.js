#!/usr/bin/env node

//Copyright (C) 2018 Intel Corporation
// 
//SPDX-License-Identifier: MIT
//
//MIT License
//
//Copyright (c) 2018 Intel Corporation
//
//Permission is hereby granted, free of charge, to any person obtaining a copy of
//this software and associated documentation files (the "Software"),
//to deal in the Software without restriction, including without limitation the rights to
//use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
//and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
//INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
//AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
//DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

const WebSocket = require('ws');
const fs = require('fs');
const readline = require('readline');
const colors = require("colors");

let con = '';
let pipe_id = 0;

let m = new Map();
for (let i = 0; i < 100; i++) {
    m.set(i, 0);
}

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});


const filename = 'hostname.txt';
let url = 0;
function read_server_ip() {
    let array = fs.readFileSync(filename).toString().split("\n");
    array = array.filter(function (e) { return e });
    console.log("HERE ARE SERVER HOSTNAME LISTS:".yellow);
    for (i in array) {
        console.log((i + " , " + array[i]).blue);
    }
    rl.question('Please chose server by id: '.magenta, (answer) => {
        if (array[answer] !== undefined) {
            url = array[answer];
            set_websocket();

        } else {
            console.log("NO such server!!!please chose again".red);
            read_server_ip();
        }

    });
}

function set_websocket() {
    const ws = new WebSocket("wss://" + url + ":8124/routeData", {
        ca: fs.readFileSync('./cert_client_8126_8124/ca-crt.pem'),
        key: fs.readFileSync('./cert_client_8126_8124/client1-key.pem'),
        cert: fs.readFileSync('./cert_client_8126_8124/client1-crt.pem'),
        rejectUnauthorized: true,
        requestCert: true
    });


    ws.on('open', function () {
        console.log(`[receiver] open`.yellow);
    });

    ws.on('message', function (data) {
        console.log("[receiver] Received:", data);
        count = 0;
        if (typeof data === 'string') {
            if (data.indexOf('=') > 0)
                return;
        }


        pipe_id = data[0];
        con = 'pipe_' + pipe_id.toString();
        let path = './' + con;
        console.log(("output dir: ", path).bgMagenta);

        try {
            fs.accessSync(path, fs.constants.F_OK);
        } catch (ex) {
            console.log(("access error for ", con).red);
            if (ex) {
                console.log(("please build a new directory for ", con).red);
                fs.mkdir(con, function (err) {
                    if (err) {
                        console.log(("Failed to create dir, err =  ", err).red);
                    }
                });
            } else {
                console.log(("output data will be put into ", path).green);
                return;
            }
        };

        if (data.byteLength > 1024) {
            let temp = m.get(pipe_id);
            let buff = new Buffer(data);
            let image_name = 'image_' + temp + '.jpg';
            let path = './' + con + '/' + image_name;
            let fd = fs.openSync(path, 'w');
            fs.write(fd, buff, 4, buff.length - 4, 0, function (err, written) {
                console.log('err', err);
                console.log('written', written);
            });
            temp++;
            m.set(pipe_id, temp);
        } else {
            let buff = new Buffer(data);
            let path = './' + con + '/output.txt';
            fs.appendFile(path, buff.toString('utf8', 4, buff.length) + "\n", function (err) {
                if (err) {
                    console.log("append failed: ", err);
                } else {
                    // done
                    console.log("done".green);
                }
            })
        }
    });

    ws.on('error', function () {
        console.log(`Receiver already CONNECTED to server!`.red);

    });

    ws.on("close", function () {
        console.log(`[receiver] close`.yellow);
    });

}

read_server_ip();

