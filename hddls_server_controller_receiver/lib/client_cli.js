'use strict';

const readline = require('readline');
const SecureWebsocket = require('./secure_client');

class clientCLI {
    constructor(options) {
        this._options = Object.assign(
            {
                host: undefined,
                port: undefined,
                completer: undefined,
                tips: undefined
            },
            options
        );
        this._rl = readline.createInterface({
            input: process.stdin,
            output: process.stdout,
            prompt: 'command> ',
            completer: options.completer
        });
        this._ws = null;
        this._exec = defaultExec;

        this._rl.on('close', ()=>{
            !! this._ws && this._ws.close()
        });

        this._rl.on('connect', (exec, ws, rl)=> {
            rl.on("line", (line) => {
                let fn = exec(line);
                rl.pause();
                if(!! fn) {
                    fn.length === 2 && fn(ws, rl);
                }
            });
           ws.on('open', ()=> rl.prompt());
        });

        this._rl.on('hint', (hint)=> {
            this._rl.pause();
            console.log(hint);
            this._rl.prompt();
        })
    }

    start() {

        var recursiveAsyncReadLine = getRecursiveAsyncReadLine(this._rl);
        var answerHandler = getAnswerHandler.call(this, 'please enter server host & port\n', recursiveAsyncReadLine);
        recursiveAsyncReadLine('please enter server host & port\n', answerHandler);
    }

    setExec(customExec) {
        this._exec = customExec;
    }

    setInParser(customIn) {
        this._inParser = customIn || incoming;
    }
    close() {
        this._rl.emit('close');
    }

    prependListener(message, ...Args) {
        if(['connect'].includes(message)) {
            this._rl.on(message, ...Args);
        }
    }
}

function isString(str) {
    return typeof(str) == 'string' || str instanceof String;
}
function getAnswerHandler(question, callback) 
{
    var that = this;
    var handler = function (answer) {
        answer = answer.trim();
        const url = answer.trim().split(':');
        if(!answer || url.length == 2)
        {
            !answer && console.log(`use default setting ${that._options.host}:${that._options.port}`);
            !!answer && (that._options.host = url[0],that._options.port = url[1]);
            that._ws = new SecureWebsocket(that._options);
            that._ws.on("message", (data)=> {that._inParser(that._ws, data);that._rl.prompt();});
            that._ws.on('close', ()=>{that._rl.removeAllListeners('line');console.log("\nserver close the connection, please close client by CTRL + C");that._ws = null});
            that._rl.emit('connect', that._exec, that._ws, that._rl);

        } else {
            callback.call(that, question, handler);
        }
    }

    return handler;
}
function getRecursiveAsyncReadLine(rl)
{   var that = rl;
    return function recursiveAsyncReadLine(question, fn) {
        that.question(question, fn);
    }
}


function defaultExec(line, ws) {
    ws.send(line);
}

function incoming(ws, message) {
    console.log(`Default handler ${message}`);
};

module.exports = clientCLI;