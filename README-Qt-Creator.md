# Open this project in Qt Creator

1. In Qt Creator, choose **File → Open File or Project**.
2. Open `CMakeLists.txt` in this directory. If you have a source archive, extract it first.
3. Choose a Qt kit with Qt Widgets, Network, and Multimedia installed, then configure and build.

On macOS, the kit also needs Qt Concurrent. Enable iCloud Drive in System Settings for the Apple Account that hosts invoices. Submitting the Invoice Form creates a local PDF and automatically publishes a read-only iCloud download link. Use **Publish invoice PDF** to renew an expired link from the saved PDF.

## Invoice payments

1. Deploy the website and its Firestore rules from the Website project.
2. In Firebase Authentication, find each technician’s user UID. In the Firestore console, add an `invoiceStaff/<uid>` document for each authorized technician.
3. On the Mac that publishes invoices, sign in and open **Square → Set up Square**. Enter a production Square API token and the Square location ID. The app verifies that the USD location accepts cards and stores the credentials in macOS Keychain. **Preferences** displays the connection status.

Publish an invoice, then open its **Public record** and select **Generate Square link**. The app creates a single-use checkout for the grand total. Customers can select **Pay now** on the website when the inquiry is **Ready For Pickup**. Keep the app open and signed in on a Mac with Square connected so it can verify card payments, mark the inquiry **Done!**, and unlock the itemized PDF.

For payments received outside Square, use **Review invoice payment** on the Public record tab to record receipt, then mark the inquiry **Done!** to make the PDF available.
