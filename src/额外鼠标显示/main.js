
var w = floaty.rawWindow(
    <frame gravity="center">
        {/* <text text="◎" textColor="#D90051" w='256' h='256' textStyle="bold" /> */}
        <img src="data:image/png;base64,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" w='32' h='32' />
    </frame>
);
w.setTouchable(false)
w.setPosition(-500, -500)
// w.setPosition(500, 500)

const devWitdh = device.width;
const devHeight = device.height;

try {
    toast("虚拟光标已启动");
    udpSocket = new java.net.DatagramSocket(6533)
    bytes = java.lang.reflect.Array.newInstance(java.lang.Byte.TYPE, 1024)
    dataGramPack = new java.net.DatagramPacket(bytes, 1024)
    var runningFlag = true
    while (runningFlag) {
        udpSocket.receive(dataGramPack);
        data = dataGramPack.getData();
        length = dataGramPack.getLength();
        msg = new java.lang.String(data, 0, length);
        if (msg.indexOf(",") === -1) {
            break
        } else {
            var xy = msg.split(",")
            x = parseInt(xy[0]);
            y = parseInt(xy[1]);
            w.setPosition(x-48, y-48);
        }
        
    }
    udpSocket.close();
    w.close();
    toast("虚拟光标已关闭");

}
catch (err) {
    toast(err);
    udpSocket.close();
    w.close();
    toast("虚拟光标已关闭");

}