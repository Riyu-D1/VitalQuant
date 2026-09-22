import { defineConfig, type RegistryItem } from 'jsrepo';
import { output } from '@jsrepo/shadcn';
import { type Category, componentMetadata, type Variant } from './src/constants/Information';

// Components that have non-code assets which should use manual dependency resolution.
// Key format: `${category}/${title}` to keep it unique.
const MANUAL_ASSETS: Record<string, { path: string; dependencyResolution: 'manual' }[]> = {
  'Components/Lanyard': [
    { path: 'card.glb', dependencyResolution: 'manual' },
    { path: 'lanyard.png', dependencyResolution: 'manual' }
  ]

  // Example: if ModelViewer had a model file you wanted to mark as manual too:
  // 'Components/ModelViewer': [
  //   { path: 'model.glb', dependencyResolution: 'manual' }
  // ]
};

export default defineConfig({
  registry: {
    name: '@react-bits',
    description:
      'An open source collection of animated, interactive & fully customizable React components for building stunning, memorable user interfaces.',
    homepage: 'https://reactbits.dev',
    authors: ['David Haz'],
    bugs: 'https://github.com/DavidHDev/react-bits/issues',
    repository: 'https://github.com/DavidHDev/react-bits',
    tags: [
      'react',
      'javascript',
      'components',
      'web',
      'reactjs',
      'css-animations',
      'component-library',
      'ui-components',
      '3d',
      'ui-library',
      'tailwind',
      'tailwindcss',
      'components',
      'components-library'
    ],
    excludeDeps: ['react'],
    outputs: [output({ dir: 'public/r', format: true })],
    items: [
      ...Object.values(componentMetadata).map(component =>
        defineComponent({
          title: component.name,
          description: component.description,
          category: component.category,
          categories: [component.category],
          meta: component.meta,
          variants: component.variants
        })
      )
    ].flat()
  }
});

/**
 * Define a component to be exposed from the registry. Creates the 4 different variants of the component and ensures the correct files are included.
 *
 * @param title The title of the component.
 * @param description The description of the component.
 * @param category The category of the component.
 * @param categories Organize the component into multiple categories.
 * @param meta Optional meta data for the component.
 * @param variants The variants of the component that are available through the registry (default: all variants)
 * @returns An array of RegistryItem objects.
 */
function defineComponent({
  title,
  description,
  category,
  categories,
  meta,
  variants = ['JS-CSS', 'JS-TW', 'TS-CSS', 'TS-TW']
}: {
  title: string;
  description: string;
  category: Category;
  categories?: string[];
  meta?: Record<string, string>;
  variants?: readonly Variant[];
}): RegistryItem[] {
  const baseItem: Omit<RegistryItem, 'files' | 'name'> = {
    title,
    description,
    type: 'registry:component',
    categories: [category, ...(categories ?? [])],
    meta
  };

  // Unique key for this component in MANUAL_ASSETS
  const key = `${category}/${title}`;

  const manualFiles = MANUAL_ASSETS[key] && MANUAL_ASSETS[key].length > 0 ? MANUAL_ASSETS[key] : [];

  const withManualFiles = (basePath: string) =>
    manualFiles.length > 0
      ? [
          {
            path: basePath,
            files: manualFiles
          }
        ]
      : [
          {
            path: basePath
          }
        ];

  // this might warrant a bit of explanation
  // basically we check if the variant is included in the variants array and if so we return the item as part of an array
  // otherwise we return an empty array
  // we then spread that array empty or otherwise into the return array
  return [
    // JS + CSS
    ...(variants.includes('JS-CSS')
      ? [
          {
            ...baseItem,
            name: `${baseItem.title}-JS-CSS`,
            files: withManualFiles(`src/content/${category}/${title}`)
          }
        ]
      : []),

    // JS + Tailwind
    ...(variants.includes('JS-TW')
      ? [
          {
            ...baseItem,
            name: `${baseItem.title}-JS-TW`,
            files: withManualFiles(`src/tailwind/${category}/${title}`)
          }
        ]
      : []),

    // TS + CSS
    ...(variants.includes('TS-CSS')
      ? [
          {
            ...baseItem,
            name: `${baseItem.title}-TS-CSS`,
            files: withManualFiles(`src/ts-default/${category}/${title}`)
          }
        ]
      : []),

    // TS + Tailwind
    ...(variants.includes('TS-TW')
      ? [
          {
            ...baseItem,
            name: `${baseItem.title}-TS-TW`,
            files: withManualFiles(`src/ts-tailwind/${category}/${title}`)
          }
        ]
      : [])
  ];
}
