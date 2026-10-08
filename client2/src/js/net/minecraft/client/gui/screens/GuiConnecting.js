import GuiScreen from "../GuiScreen.js";
import GuiButton from "../widgets/GuiButton.js";
import Connection from "../../../../../../../adapter/connection.mjs";
import ProtocolState from "../../network/ProtocolState.js";

export default class GuiConnecting extends GuiScreen {

    constructor(previousScreen, address) {
        super();

        this.previousScreen = previousScreen;
        this.connecting = false;
        this.networkManager = null;

        this.address = address;
    }

    connect(address) {
        this.networkManager = new Connection(this.minecraft);
        this.minecraft.lapisConnection = this.networkManager;
        this.networkManager.connect(address);
    }

    init() {
        super.init();

        let y = this.height / 2 - 50;
        this.buttonList.push(new GuiButton("Cancel", this.width / 2 - 100, y + 130, 200, 20, () => {
            this.minecraft.displayScreen(this.previousScreen);
        }));

        // Connect on first initialization
        if (!this.connecting) {
            this.connecting = true;
            this.connect(this.address);
        }
    }

    drawScreen(stack, mouseX, mouseY, partialTicks) {
        // Render dirt background
        this.drawBackground(stack, this.textureBackground, this.width, this.height);

        // Render title
        this.drawCenteredString(stack, "Connecting to server...", this.width / 2, this.height / 2 - 20);

        super.drawScreen(stack, mouseX, mouseY, partialTicks);
    }


    onClose() {
        super.onClose();

        if (this.networkManager !== null && this.networkManager.getState() !== ProtocolState.PLAY) {
            this.networkManager.close();
        }
    }
}