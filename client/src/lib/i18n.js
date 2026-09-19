/**
 * Interface strings for the React client.
 *
 * Coverage note, deliberately visible in Settings: the English strings are
 * written here. The Malay, Simplified Chinese and Tamil strings were produced
 * without a native reviewer and are marked as needing review. They are shipped
 * because dropping them would lose language support the product already had,
 * not because they are signed off.
 *
 * The legacy interface in web/ carries a larger translated vocabulary
 * (web/i18n.js). Strings still only present there are listed in
 * docs/FRONTEND_MIGRATION.md.
 */

export const LANGUAGES = [
  { code: 'en', name: 'English' },
  { code: 'ms', name: 'Bahasa Melayu' },
  { code: 'zh', name: '简体中文' },
  { code: 'ta', name: 'தமிழ்' },
];

/** Languages whose strings have not been checked by a native speaker. */
export const NEEDS_REVIEW = ['ms', 'zh', 'ta'];

const STRINGS = {
  en: {
    settings: 'Settings',
    appearance: 'appearance',
    theme_system: 'Follow the system',
    theme_light: 'Light',
    theme_dark: 'Dark',
    theme_note: 'The system setting is used unless you choose otherwise.',
    text_size: 'text size',
    text_normal: 'Normal',
    text_large: 'Large',
    text_larger: 'Larger',
    motion: 'motion',
    motion_system: 'Follow the system',
    motion_reduced: 'Reduce motion',
    language: 'language',
    translation_review: 'This translation needs review by a native speaker.',
  },
  ms: {
    settings: 'Tetapan',
    appearance: 'rupa',
    theme_system: 'Ikut sistem',
    theme_light: 'Cerah',
    theme_dark: 'Gelap',
    theme_note: 'Tetapan sistem digunakan melainkan anda memilih yang lain.',
    text_size: 'saiz teks',
    text_normal: 'Biasa',
    text_large: 'Besar',
    text_larger: 'Lebih besar',
    motion: 'gerakan',
    motion_system: 'Ikut sistem',
    motion_reduced: 'Kurangkan gerakan',
    language: 'bahasa',
    translation_review: 'Terjemahan ini perlu disemak oleh penutur asli.',
  },
  zh: {
    settings: '设置',
    appearance: '外观',
    theme_system: '跟随系统',
    theme_light: '浅色',
    theme_dark: '深色',
    theme_note: '除非另行选择，否则使用系统设置。',
    text_size: '文字大小',
    text_normal: '标准',
    text_large: '大',
    text_larger: '更大',
    motion: '动效',
    motion_system: '跟随系统',
    motion_reduced: '减少动效',
    language: '语言',
    translation_review: '此翻译需要母语者校对。',
  },
  ta: {
    settings: 'அமைப்புகள்',
    appearance: 'தோற்றம்',
    theme_system: 'கணினியைப் பின்பற்று',
    theme_light: 'வெளிர்',
    theme_dark: 'கரும்',
    theme_note: 'நீங்கள் வேறு ஒன்றைத் தேர்ந்தெடுக்காத வரை கணினி அமைப்பு பயன்படுத்தப்படும்.',
    text_size: 'எழுத்து அளவு',
    text_normal: 'இயல்பு',
    text_large: 'பெரிது',
    text_larger: 'மிகப் பெரிது',
    motion: 'அசைவு',
    motion_system: 'கணினியைப் பின்பற்று',
    motion_reduced: 'அசைவைக் குறை',
    language: 'மொழி',
    translation_review: 'இந்த மொழிபெயர்ப்பை தாய்மொழி பேசுபவர் சரிபார்க்க வேண்டும்.',
  },
};

export function t(lang, key) {
  return (STRINGS[lang] && STRINGS[lang][key]) || STRINGS.en[key] || key;
}
