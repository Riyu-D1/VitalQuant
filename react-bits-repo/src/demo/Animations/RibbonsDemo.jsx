import { useMemo } from 'react';
import { CodeTab, PreviewTab, TabsLayout } from '../../components/common/TabsLayout';
import { Box, Flex, IconButton, Text } from '@chakra-ui/react';
import { FiMinus, FiPlus } from 'react-icons/fi';

import CodeExample from '../../components/code/CodeExample';
import useComponentProps from '../../hooks/useComponentProps';
import { ComponentPropsProvider } from '../../components/context/ComponentPropsContext';

import PropTable from '../../components/common/Preview/PropTable';
import Dependencies from '../../components/code/Dependencies';
import PreviewSlider from '../../components/common/Preview/PreviewSlider';
import PreviewSwitch from '../../components/common/Preview/PreviewSwitch';
import Customize from '../../components/common/Preview/Customize';

import OpenInStudioButton from '../../components/common/Preview/OpenInStudioButton';

import Ribbons from '../../content/Animations/Ribbons/Ribbons';
import { ribbons } from '../../constants/code/Animations/ribbonsCode';

const DEFAULT_PROPS = {
  baseThickness: 30,
  colors: ['#5227FF'],
  speedMultiplier: 0.5,
  maxAge: 500,
  enableFade: false,
  enableShaderEffect: false
};

const RibbonsDemo = () => {
  const { props, updateProp, resetProps, hasChanges } = useComponentProps(DEFAULT_PROPS);
  const { baseThickness, colors, speedMultiplier, maxAge, enableFade, enableShaderEffect } = props;

  const propData = useMemo(
    () => [
      {
        name: 'colors',
        type: 'string[]',
        default: "['#5227FF']",
        description: 'An array of color strings to be used for the ribbons.'
      },
      {
        name: 'baseSpring',
        type: 'number',
        default: '0.03',
        description: 'Base spring factor for the physics controlling ribbon motion.'
      },
      {
        name: 'baseFriction',
        type: 'number',
        default: '0.9',
        description: 'Base friction factor that dampens the ribbon motion.'
      },
      {
        name: 'baseThickness',
        type: 'number',
        default: '30',
        description: 'The base thickness of the ribbons.'
      },
      {
        name: 'offsetFactor',
        type: 'number',
        default: '0.02',
        description: 'A factor to horizontally offset the starting positions of the ribbons.'
      },
      {
        name: 'maxAge',
        type: 'number',
        default: '500',
        description: 'Delay in milliseconds controlling how long the ribbon trails extend.'
      },
      {
        name: 'pointCount',
        type: 'number',
        default: '50',
        description: 'The number of points that make up each ribbon.'
      },
      {
        name: 'speedMultiplier',
        type: 'number',
        default: '0.5',
        description: 'Multiplier that adjusts how fast trailing points interpolate towards the head.'
      },
      {
        name: 'enableFade',
        type: 'boolean',
        default: 'true',
        description: 'If true, a fade effect is applied along the length of the ribbon.'
      },
      {
        name: 'enableShaderEffect',
        type: 'boolean',
        default: 'true',
        description: 'If true, an additional sine-wave shader effect is applied to the ribbons.'
      },
      {
        name: 'effectAmplitude',
        type: 'number',
        default: '2',
        description: 'The amplitude of the shader displacement effect.'
      },
      {
        name: 'backgroundColor',
        type: 'number[]',
        default: '[0, 0, 0, 0]',
        description: 'An RGBA array specifying the clear color for the renderer.'
      }
    ],
    []
  );

  return (
    <ComponentPropsProvider props={props} defaultProps={DEFAULT_PROPS} resetProps={resetProps} hasChanges={hasChanges}>
      <TabsLayout>
        <PreviewTab>
          <Box position="relative" className="demo-container" h={400} p={0} overflow="hidden">
            <Text position="absolute" fontSize="clamp(2rem, 6vw, 6rem)" fontWeight={900} color="#271E37">
              Hover Me.
            </Text>
            <Ribbons
              baseThickness={baseThickness}
              colors={colors}
              speedMultiplier={speedMultiplier}
              maxAge={maxAge}
              enableFade={enableFade}
              enableShaderEffect={enableShaderEffect}
            />
          </Box>

          <Flex justify="flex-end" mt={2} mb={-2}>
            <OpenInStudioButton
              backgroundId="ribbons"
              currentProps={{
                baseThickness,
                colors,
                speedMultiplier,
                maxAge,
                enableFade,
                enableShaderEffect
              }}
              defaultProps={DEFAULT_PROPS}
            />
          </Flex>

          <Customize>
            <Flex gap={4} align="center" mt={4}>
              <Text fontSize="sm">Count</Text>
              <IconButton
                onClick={() => colors.length > 1 && updateProp('colors', colors.slice(0, -1))}
                fontSize="xs"
                bg="#170D27"
                borderRadius="10px"
                border="1px solid #271E37"
                _hover={{ bg: '#271E37' }}
                color="#fff"
                h={10}
              >
                <FiMinus />
              </IconButton>
              <Text>{colors.length}</Text>
              <IconButton
                fontSize="xs"
                bg="#170D27"
                borderRadius="10px"
                border="1px solid #271E37"
                _hover={{ bg: '#271E37' }}
                color="#fff"
                h={10}
                onClick={() => {
                  if (colors.length < 10) {
                    const newColor = `#${Math.floor(Math.random() * 16777215)
                      .toString(16)
                      .padStart(6, '0')}`;
                    updateProp('colors', [...colors, newColor]);
                  }
                }}
              >
                <FiPlus />
              </IconButton>
            </Flex>

            <PreviewSlider
              title="Thickness"
              min={1}
              max={60}
              step={1}
              value={baseThickness}
              onChange={val => updateProp('baseThickness', val)}
            />

            <PreviewSlider
              title="Speed"
              min={0.3}
              max={0.7}
              step={0.01}
              value={speedMultiplier}
              onChange={val => updateProp('speedMultiplier', val)}
            />

            <PreviewSlider
              title="Max Age"
              min={300}
              max={1000}
              step={100}
              value={maxAge}
              onChange={val => updateProp('maxAge', val)}
            />

            <PreviewSwitch
              title="Enable Fade"
              isChecked={enableFade}
              onChange={checked => updateProp('enableFade', checked)}
            />

            <PreviewSwitch
              title="Enable Waves"
              isChecked={enableShaderEffect}
              onChange={checked => updateProp('enableShaderEffect', checked)}
            />
          </Customize>

          <PropTable data={propData} />
          <Dependencies dependencyList={['ogl']} />
        </PreviewTab>

        <CodeTab>
          <CodeExample codeObject={ribbons} componentName="Ribbons" />
        </CodeTab>
      </TabsLayout>
    </ComponentPropsProvider>
  );
};

export default RibbonsDemo;
