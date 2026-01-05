const { app } = require('@azure/functions');
const { BlobServiceClient, generateBlobSASQueryParameters, BlobSASPermissions, StorageSharedKeyCredential } = require('@azure/storage-blob');
const twilio = require('twilio');

app.eventHub('processAlert', {
    connection: 'IOTHUB_CONNECTION',
    eventHubName: 'iothub-ehub-security-h-55730615-7439d09be8',
    cardinality: 'many',
    dataType: 'binary', // 🔥 Binary parser ko barkarar rakha gaya hai
    handler: async (messages, context) => {
        context.log(`--- Processing ${messages.length} events ---`);

        const storageConnString = process.env["STORAGE_CONNECTION"];
        const blobServiceClient = BlobServiceClient.fromConnectionString(storageConnString);
        const containerClient = blobServiceClient.getContainerClient("alerts");

        // Twilio Client Initialize
        const twilioClient = new twilio(process.env["TWILIO_SID"], process.env["TWILIO_TOKEN"]);

        for (let message of messages) {
            try {
                const buffer = Buffer.from(message);
                const timestamp = Date.now();
                const randomId = Math.floor(Math.random() * 1000);

                let fileName, contentType, isImage = false;

                // 1. File Type Check (Existing Logic)
                if (buffer[0] === 0xFF && buffer[1] === 0xD8) {
                    fileName = `capture-${timestamp}-${randomId}.jpg`;
                    contentType = 'image/jpeg';
                    isImage = true;
                    context.log("📸 Photo Detected!");
                } else {
                    fileName = `alert-${timestamp}-${randomId}.json`;
                    contentType = 'application/json';
                    context.log("📝 Text Alert Detected!");
                }

                // 2. Storage mein Upload (Existing Logic)
                const blockBlobClient = containerClient.getBlockBlobClient(fileName);
                await blockBlobClient.upload(buffer, buffer.length, {
                    blobHTTPHeaders: { blobContentType: contentType }
                });
                context.log(`✅ Success: Saved ${fileName}`);

                // 3. AGAR PHOTO HAI TO WHATSAPP BHEJEIN (New Logic)
                if (isImage) {
                    const sasToken = await generateSasToken(fileName, storageConnString);
                    const imageUrl = `${blockBlobClient.url}?${sasToken}`;

                    await twilioClient.messages.create({
                        from: 'whatsapp:+14155238886', // Twilio Sandbox Number
                        to: process.env["TO_WHATSAPP"],
                        body: `🚨 *Security Alert!* \nMotion detected at your camera. \nTime: ${new Date().toLocaleString()}`,
                        mediaUrl: [imageUrl]
                    });
                    context.log("📱 WhatsApp Alert Sent successfully!");
                }

            } catch (err) {
                context.log(`❌ Error processing message: ${err.message}`);
            }
        }
    }
});

// SAS Token generate karne wala function
async function generateSasToken(blobName, connString) {
    const matches = connString.match(/AccountName=([^;]+);AccountKey=([^;]+)/);
    const accountName = matches[1];
    const accountKey = matches[2];

    const sharedKeyCredential = new StorageSharedKeyCredential(accountName, accountKey);
    
    const sasOptions = {
        containerName: "alerts",
        blobName: blobName,
        permissions: BlobSASPermissions.parse("r"), // Read access
        startsOn: new Date(),
        expiresOn: new Date(new Date().valueOf() + 3600 * 1000) // 1 ghante ke liye valid
    };

    return generateBlobSASQueryParameters(sasOptions, sharedKeyCredential).toString();
}