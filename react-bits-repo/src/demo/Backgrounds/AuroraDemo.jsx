import { useMemo } from 'react';
import { CodeTab, PreviewTab, TabsLayout } from '../../components/common/TabsLayout';
import { Box, Flex, Text } from '@chakra-ui/react';

import CodeExample from '../../components/code/CodeExample';

import PropTable from '../../components/common/Preview/PropTable';
import Dependencies from '../../components/code/Dependencies';
import useForceRerender from '../../hooks/useForceRerender';
import useComponentProps from '../../hooks/useComponentProps';
import Customize from '../../components/common/Preview/Customize';
import PreviewSlider from '../../components/common/Preview/PreviewSlider';
import BackgroundContent from '../../components/common/Preview/BackgroundContent';
import OpenInStudioButton from '../../components/common/Preview/OpenInStudioButton';
import { ComponentPropsProvider } from '../../components/context/ComponentPropsContext';

import Aurora from '../../content/Backgrounds/Aurora/Aurora';
import { aurora } from '../../constants/code/Backgrounds/auroraCode';

// Default prop values for this component
const DEFAULT_PROPS = {
  color1: '#7cff67',
  color2: '#B19EEF',
  color3: '#5227FF',
  speed: 1,
  blend: 0.5
};

const AuroraDemo = () => {
  const { props, updateProp, resetProps, hasChanges } = useComponentProps(DEFAULT_PROPS);
  const { color1, color2, color3, speed, blend } = props;

  const [key, forceRerender] = useForceRerender();

  const propData = useMemo(
    () => [
      {
        name: 'colorStops',
        type: '[string, string, string]',
        default: '["#3A29FF", "#FF94B4", "#FF3232"]',
        description: 'An array of three hex colors defining the aurora gradient.'
      },
      {
        name: 'speed',
        type: 'number',
        default: '1.0',
        description: 'Controls the animation speed. Higher values make the aurora move faster.'
      },
      {
        name: 'blend',
        type: 'number',
        default: '0.5',
        description: 'Controls the blending of the aurora effect with the background.'
      },
      {
        name: 'amplitude',
        type: 'number',
        default: '1.0',
        description: 'Controls the height intensity of the aurora effect.'
      }
    ],
    []
  );

  return (
    <ComponentPropsProvider
      props={props}
      defaultProps={DEFAULT_PROPS}
      resetProps={resetProps}
      hasChanges={hasChanges}
      demoOnlyProps={['color1', 'color2', 'color3']}
      computedProps={{ colorStops: [color1, color2, color3] }}
    >
      <TabsLayout>
        <PreviewTab>
          <Box position="relative" className="demo-container" h={600} p={0} overflow="hidden">
            <Aurora key={key} blend={blend} speed={speed} colorStops={[color1, color2, color3]} />

            {/* For Demo Purposes Only */}
            <BackgroundContent pillText="New Background" headline="Bring the Arctic to you, with one line of code" />
          </Box>

          <Flex justify="flex-end" mt={2} mb={-2}>
            <OpenInStudioButton
              backgroundId="aurora"
              currentProps={{ colorStops: [color1, color2, color3], speed, blend }}
              defaultProps={{ colorStops: ['#5227FF', '#7cff67', '#5227FF'], speed: 1, blend: 0.5 }}
            />
          </Flex>

          <Customize>
            <Flex gap={4} mb={2} align="center" wrap="wrap">
              <Flex alignItems="center">
                <Text mr={2}>Color 1</Text>
                <input
                  type="color"
                  value={color1}
                  style={{ height: '22px', outline: 'none', border: 'none' }}
                  onChange={e => {
                    updateProp('color1', e.target.value);
                    forceRerender();
                  }}
                />
              </Flex>

              <Flex alignItems="center">
                <Text mr={2}>Color 2</Text>
                <input
                  type="color"
                  value={color2}
                  style={{ height: '22px', outline: 'none', border: 'none' }}
                  onChange={e => {
                    updateProp('color2', e.target.value);
                    forceRerender();
                  }}
                />
              </Flex>

              <Flex alignItems="center">
                <Text mr={2}>Color 3</Text>
                <input
                  type="color"
                  value={color3}
                  style={{ height: '22px', outline: 'none', border: 'none' }}
                  onChange={e => {
                    updateProp('color3', e.target.value);
                    forceRerender();
                  }}
                />
              </Flex>
            </Flex>

            <PreviewSlider
              title="Speed"
              min={0}
              max={2}
              step={0.1}
              value={speed}
              onChange={val => {
                updateProp('speed', val);
                forceRerender();
              }}
            />

            <PreviewSlider
              title="Blend"
              min={0}
              max={1}
              step={0.01}
              value={blend}
              onChange={val => {
                updateProp('blend', val);
                forceRerender();
              }}
            />
          </Customize>

          <PropTable data={propData} />
          <Dependencies dependencyList={['ogl']} />
        </PreviewTab>

        <CodeTab>
          <CodeExample codeObject={aurora} componentName="Aurora" />
        </CodeTab>
      </TabsLayout>
    </ComponentPropsProvider>
  );
};

export default AuroraDemo;
