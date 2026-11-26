# Open this project in Qt Creator

1. In Qt Creator, choose **File → Open File or Project**.
2. Open `CMakeLists.txt` in this directory. If you have a source archive, extract it first.
3. Choose a Qt kit with Qt Widgets, Network, and Multimedia installed, then configure and build.

On macOS, the kit also needs Qt Concurrent. Enable iCloud Drive in System Settings for the Apple Account that hosts invoices. Submitting the Invoice Form creates a local PDF and automatically publishes a read-only iCloud download link. Use **Publish invoice PDF** to renew an expired link from the saved PDF.

## Invoice payments

1. Deploy the website and its Firestore rules from the Website project.
2. In Firebase Authentication, find each technician’s user UID. In the Firestore console, add an `invoiceStaff/<uid>` document for each authorized technician.
3. On the Mac that publishes invoices, sign in and open **Preferences → Square payments**. Enter a production Square API token and the Square location ID. The app verifies the USD location and stores these credentials in macOS Keychain.

Set the inquiry to **Ready For Pickup** and publish the invoice. The customer can then choose Card, Cash, Zelle, or Other on the website. Publishing prepares a single-use Square checkout for the grand total. Keep the app open and signed in on a Mac with Square connected so it can verify card payments, mark the inquiry **Done!**, and unlock the itemized PDF.

Cash, Zelle, and Other send the inquiry to **Pending Payment Review** when the customer confirms payment. Use **Review invoice payment** on the Public record tab to approve payment received, then mark the inquiry **Done!** to make the PDF available.
