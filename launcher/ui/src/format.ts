export function formatBytes(bytes: number): string {
  if (bytes < 1024 * 1024) return `${Math.max(1, Math.round(bytes / 1024))} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(bytes < 100 * 1024 * 1024 ? 1 : 0)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

/** 069a79f444e94726a5befca90e38aaf5 -> 069a79f4-44e9-4726-a5be-fca90e38aaf5 */
export function dashedUuid(uuid: string): string {
  return uuid.length === 32 ? `${uuid.slice(0, 8)}-${uuid.slice(8, 12)}-${uuid.slice(12, 16)}-${uuid.slice(16, 20)}-${uuid.slice(20)}` : uuid;
}

export const buyUrl = 'https://www.minecraft.net/store/minecraft-java-bedrock-edition-pc';
export const profileUrl = 'https://www.minecraft.net/msaprofile/mygames/editprofile';

export interface ErrorMeta {
  category: string;
  title: string;
  description: string;
  action?: { label: string; url: string };
}

/** What to show for an error code from the launcher: a heading, a plain sentence, and where to go to fix it. */
export function describeError(code?: string, rawMessage?: string): ErrorMeta {
  switch (code) {
    case 'xbox_no_account':
      return {
        category: 'Xbox profile',
        title: 'This account has no Xbox profile yet',
        description: 'Minecraft signs in through Xbox. Creating a gamer profile is free and takes a minute on xbox.com. Then come back and try again.',
        action: { label: 'Open xbox.com', url: 'https://www.xbox.com/' },
      };
    case 'xbox_child':
      return {
        category: 'Family safety',
        title: 'A parent needs to approve this account',
        description: 'This is a child account. An adult has to add it to a Microsoft family group before it can use Minecraft online services.',
        action: { label: 'Microsoft family settings', url: 'https://account.microsoft.com/family/' },
      };
    case 'xbox_adult':
      return {
        category: 'Verification',
        title: 'Adult verification needed',
        description: 'Xbox requires adult verification on this account before it can be used.',
        action: { label: 'Verify on xbox.com', url: 'https://account.xbox.com/' },
      };
    case 'xbox_region':
      return {
        category: 'Region',
        title: 'Xbox is not available in this region',
        description: 'Xbox services are not available in the country or region this account is registered in.',
      };
    case 'xbox_denied':
      return { category: 'Xbox', title: 'Xbox refused this account', description: rawMessage || 'Xbox did not accept this account.' };
    case 'network':
      return {
        category: 'Connection',
        title: 'Could not connect',
        description: rawMessage || 'Microsoft or Mojang could not be reached. Check your internet connection or firewall and try again.',
      };
    case 'declined':
      return { category: 'Sign-in', title: 'The sign-in was declined', description: 'The sign-in was turned down in the Microsoft window. Try again whenever you are ready.' };
    case 'window':
      return {
        category: 'Sign-in',
        title: 'The sign-in window could not open',
        description: rawMessage || 'The Microsoft sign-in window could not be shown. Try again, and if it keeps failing see the launcher logs.',
      };
    case 'invalid_grant':
      return { category: 'Sign-in', title: 'Your sign-in expired', description: 'For your safety Microsoft asks you to sign in again from time to time.' };
    case 'app_not_approved':
      return {
        category: 'App registration',
        title: 'Mojang has not approved this sign-in yet',
        description: rawMessage || 'The Microsoft application this launcher uses is not approved by Mojang.',
        action: { label: 'About approval', url: 'https://aka.ms/AppRegInfo' },
      };
    case 'not_configured':
      return { category: 'Setup', title: 'Sign-in is switched off', description: rawMessage || 'This build has no Microsoft application id.' };
    case 'service':
      return { category: 'Service', title: 'Microsoft or Mojang had a problem', description: rawMessage || 'A sign-in service answered with an error. Please try again in a moment.' };
    case 'bad_response':
      return { category: 'Service', title: 'An answer was not understood', description: rawMessage || 'A sign-in service sent something unexpected. Please try again.' };
    case 'corrupt':
      return { category: 'Download', title: 'A download did not match its checksum', description: rawMessage || 'A file from Mojang was damaged on the way. Try again.' };
    case 'disk':
      return { category: 'Disk', title: 'Could not write to disk', description: rawMessage || 'Check that the drive has room and that nothing is blocking the folder.' };
    case 'spawn':
      return { category: 'Launch', title: 'Falcon could not start', description: rawMessage || 'Windows would not start the game. Antivirus software sometimes blocks new programs.' };
    default:
      return { category: 'Error', title: 'Something went wrong', description: rawMessage || 'An unexpected error occurred.' };
  }
}
